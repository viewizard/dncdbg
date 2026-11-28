// Copyright (c) 2020-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "utils/ioredirect.h"
#include "utils/logger.h"
#include <exception>
#include <utility>
#include <vector>

namespace dncdbg
{

// Constructor: store the output callback. Pipes are created for each redirection
// session started by Exec().
IORedirect::IORedirect(OutputCallback callback)
    : m_callback(std::move(callback))
{
}

// Destructor: stop worker threads and close all remaining pipe handles.
IORedirect::~IORedirect()
{
    // The destructor must not propagate exceptions. Reset() can throw, for example when
    // join() fails, so the error is logged instead.
    try
    {
        Reset();
    }
    catch (const std::exception &e)
    {
        LOGE(log << "IORedirect destructor exception: " << e.what());
    }
    catch (...)
    {
        LOGE(log << "IORedirect destructor: unknown exception");
    }

    LOGD(log << "IORedirect: destroyed");
}

// Stop worker threads and close all pipe handles, preparing the object for a new
// redirection session. Safe to call even if no session was started before.
void IORedirect::Reset()
{
    // Signal the worker threads to stop and interrupt their pending waits, so a
    // child process that keeps its output pipes open cannot block the joins below.
    m_stopWorkers.store(true);
    SignalWakeup();

    if (m_stdoutThread.joinable())
    {
        m_stdoutThread.join();
    }
    if (m_stderrThread.joinable())
    {
        m_stderrThread.join();
    }

    // The worker threads have exited, so close the debugger-side read ends.
    ClosePipe(m_stdoutRead);
    ClosePipe(m_stderrRead);

    // Close the stdin pipes under the mutex, so a concurrent WriteStdin() cannot
    // write into a closed handle.
    {
        const std::scoped_lock lock(m_stdinMutex);
        ClosePipe(m_stdinRead);
        ClosePipe(m_stdinWrite);
    }

    // Close the child-side write ends (normally already closed by Exec()).
    ClosePipe(m_stdoutWrite);
    ClosePipe(m_stderrWrite);

    CloseWakeup();

    // The worker threads have stopped, so the next session can start new ones.
    m_stopWorkers.store(false);
}

// Execute a function with stdin/stdout/stderr redirected to the internal pipes.
// Each call starts a new redirection session: the previous session (if any) is
// finished first, and a fresh set of pipes is created.
void IORedirect::Exec(FunctionRef<void()> func)
{
    // Finish the previous session, if any.
    Reset();

    // Create a fresh set of pipes. The stdin handles are shared with WriteStdin()
    // and CloseStdin(), so the whole set is created under the stdin mutex.
    bool pipesCreated = false;
    {
        const std::scoped_lock lock(m_stdinMutex);
        pipesCreated = CreatePipe(m_stdinRead, m_stdinWrite, false);
        if (pipesCreated)
        {
            pipesCreated = CreatePipe(m_stdoutRead, m_stdoutWrite, true);
        }
        if (pipesCreated)
        {
            pipesCreated = CreatePipe(m_stderrRead, m_stderrWrite, true);
        }
    }

    if (!pipesCreated)
    {
        LOGE(log << "IORedirect: failed to create pipes");
        Reset();
        return;
    }

    if (!CreateWakeup())
    {
        LOGE(log << "IORedirect: failed to create the worker wakeup primitive");
        Reset();
        return;
    }

    // The child process must inherit only the child-side pipe ends; the debugger-side
    // ends stay private to this process.
    // The calls are short-circuited: the first failure skips the rest and the session is released.
    const bool inheritanceSet = SetInheritable(m_stdinRead, true) &&
                                SetInheritable(m_stdoutWrite, true) &&
                                SetInheritable(m_stderrWrite, true) &&
                                SetInheritable(m_stdinWrite, false) &&
                                SetInheritable(m_stdoutRead, false) &&
                                SetInheritable(m_stderrRead, false);
    if (!inheritanceSet)
    {
        LOGE(log << "IORedirect: failed to configure pipe handle inheritance");
        Reset();
        return;
    }

    // Redirect the standard streams of this process to the child-side pipe ends,
    // so the child process inherits them.
    SavedStdFiles saved = RedirectStdFiles(m_stdinRead, m_stdoutWrite, m_stderrWrite);
    if (!saved.valid)
    {
        // The standard streams are already back in place, so the child must not be started.
        LOGE(log << "IORedirect: failed to redirect the standard streams");
        Reset();
        return;
    }

    try
    {
        func();
    }
    catch (...)
    {
        // The child process was not created: put the standard streams back and
        // release the session before propagating the error.
        RestoreStdFiles(saved);
        Reset();
        throw;
    }

    // Restore the original standard file descriptors.
    RestoreStdFiles(saved);

    // The child process has inherited copies of the child-side ends; close our copies
    // so that reads on the debugger side report EOF when the child process exits.
    {
        const std::scoped_lock lock(m_stdinMutex);
        ClosePipe(m_stdinRead);
    }
    ClosePipe(m_stdoutWrite);
    ClosePipe(m_stderrWrite);

    // Start worker threads to read from the child's stdout and stderr. Both waits are
    // interruptible, so Reset() can stop the threads at any time.
    try
    {
        m_stdoutThread = std::thread(&IORedirect::ReaderWorker, this, StreamType::Stdout);
        m_stderrThread = std::thread(&IORedirect::ReaderWorker, this, StreamType::Stderr);
    }
    catch (...)
    {
        // A worker thread could not be started: stop the one that did start, release
        // the session and propagate the error.
        Reset();
        throw;
    }
}

// Write data to the child process's stdin pipe.
int IORedirect::WriteStdin(gsl::span<const char> data)
{
    if (data.empty())
    {
        return 0;
    }

    const std::scoped_lock lock(m_stdinMutex);

    if (m_stdinWrite == invalidPipe())
    {
        return 0; // Pipe already closed.
    }

    return WritePipe(m_stdinWrite, data.data(), data.size());
}

// Close the stdin pipe to signal EOF to the child process.
void IORedirect::CloseStdin()
{
    const std::scoped_lock lock(m_stdinMutex);
    ClosePipe(m_stdinWrite);
}

// Worker thread function: reads from a pipe and calls the output callback.
void IORedirect::ReaderWorker(StreamType type)
{
    // An exception must not escape the thread entry point, otherwise the whole
    // process is terminated.
    try
    {
        // Select the appropriate pipe handle based on stream type.
        const bool isStdout = (type == StreamType::Stdout);
        const PipeHandle handle = isStdout ? m_stdoutRead : m_stderrRead;
#ifdef _WIN32
        const PipeHandle ioEvent = isStdout ? m_stdoutEvent : m_stderrEvent;
#endif

        // The buffer lives on the heap for the lifetime of the thread and is reused for
        // every chunk, so the thread stack frame stays small.
        std::vector<char> buffer(ReadBufferSize);

        while (!m_stopWorkers.load())
        {
#ifdef _WIN32
            const int bytesRead = ReadPipe(handle, ioEvent, buffer.data(), buffer.size());
#else
            if (!WaitReadable(handle))
            {
                break; // The session is being stopped, or the wait failed.
            }
            const int bytesRead = ReadPipe(handle, buffer.data(), buffer.size());
#endif

            if (bytesRead > 0)
            {
                if (!m_callback)
                {
                    continue;
                }

                // A throwing callback must not take the process down.
                try
                {
                    m_callback(type, gsl::span<char>(buffer.data(), static_cast<size_t>(bytesRead)));
                }
                catch (const std::exception &e)
                {
                    LOGE(log << "IORedirect: output callback exception: " << e.what());
                }
                catch (...)
                {
                    LOGE(log << "IORedirect: output callback: unknown exception");
                }
            }
            else if (bytesRead == 0)
            {
                // EOF: the child process closed its end of the pipe.
                LOGD(log << "IORedirect: EOF on " << (isStdout ? "stdout" : "stderr"));
                break;
            }
            else
            {
                // A failed read is expected when the session is being stopped, since
                // Reset() cancels the pending wait.
                if (!m_stopWorkers.load())
                {
                    LOGE(log << "IORedirect: read error on " << (isStdout ? "stdout" : "stderr"));
                }
                break;
            }
        }
    }
    catch (const std::exception &e)
    {
        LOGE(log << "IORedirect worker thread exception: " << e.what());
    }
    catch (...)
    {
        LOGE(log << "IORedirect worker thread: unknown exception");
    }
}

} // namespace dncdbg
