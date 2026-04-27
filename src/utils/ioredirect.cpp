// Copyright (c) 2020-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// See the LICENSE file in the project root for more information.

#include "utils/ioredirect.h"
#include "utils/logger.h"
#include <array>
#include <cassert>
#include <cerrno>

namespace dncdbg
{

// Constructor: create all three pipe pairs and store the output callback.
IORedirect::IORedirect(OutputCallback callback)
    : m_callback(std::move(callback)),
      m_stdinRead(invalidPipe()),
      m_stdinWrite(invalidPipe()),
      m_stdoutRead(invalidPipe()),
      m_stdoutWrite(invalidPipe()),
      m_stderrRead(invalidPipe()),
      m_stderrWrite(invalidPipe())
{
    // Create stdin pipe (debugger writes -> child reads).
    if (!createPipe(m_stdinRead, m_stdinWrite))
    {
        LOGE(log << "IORedirect: failed to create stdin pipe");
        return;
    }

    // Create stdout pipe (child writes -> debugger reads).
    if (!createPipe(m_stdoutRead, m_stdoutWrite))
    {
        LOGE(log << "IORedirect: failed to create stdout pipe");
        return;
    }

    // Create stderr pipe (child writes -> debugger reads).
    if (!createPipe(m_stderrRead, m_stderrWrite))
    {
        LOGE(log << "IORedirect: failed to create stderr pipe");
        return;
    }

    // Mark debugger-side pipe ends as non-inheritable (child should not inherit these).
    setInheritable(m_stdinWrite, false);
    setInheritable(m_stdoutRead, false);
    setInheritable(m_stderrRead, false);

    // Mark child-side pipe ends as inheritable (child process needs these).
    setInheritable(m_stdinRead, true);
    setInheritable(m_stdoutWrite, true);
    setInheritable(m_stderrWrite, true);
}

// Destructor: stop worker threads and close all remaining pipe handles.
IORedirect::~IORedirect()
{
    // Signal worker threads to stop.
    m_stopWorkers.store(true);

    // Close the debugger-side read ends to unblock worker threads waiting on read().
    closePipe(m_stdoutRead);
    closePipe(m_stderrRead);

    // Wait for worker threads to finish.
    if (m_stdoutThread.joinable())
    {
        m_stdoutThread.join();
    }
    if (m_stderrThread.joinable())
    {
        m_stderrThread.join();
    }

    // Close any remaining pipe handles.
    closePipe(m_stdinRead);
    closePipe(m_stdinWrite);
    closePipe(m_stdoutWrite);
    closePipe(m_stderrWrite);

    LOGD(log << "IORedirect: destroyed");
}

// Execute a function with stdin/stdout/stderr redirected to the internal pipes.
void IORedirect::exec(const std::function<void()> &func)
{
    assert(!m_execCalled && "exec() can only be called once");
    m_execCalled = true;

    // Redirect standard file descriptors to the child-side pipe ends.
    SavedStdFiles saved = redirectStdFiles(m_stdinRead, m_stdoutWrite, m_stderrWrite);

    // Execute the user-provided function (which should create the child process).
    func();

    // Restore original standard file descriptors.
    restoreStdFiles(saved);

    // Close child-side pipe ends (the child process has inherited copies of these).
    // We must close our copies so that reads on the debugger side will see EOF
    // when the child process exits.
    closePipe(m_stdinRead);
    closePipe(m_stdoutWrite);
    closePipe(m_stderrWrite);

    // Start worker threads to read from the child's stdout and stderr.
    m_stdoutThread = std::thread(&IORedirect::readerWorker, this, StreamType::Stdout);
    m_stderrThread = std::thread(&IORedirect::readerWorker, this, StreamType::Stderr);
}

// Write data to the child process's stdin pipe.
int IORedirect::writeStdin(gsl::span<const char> data)
{
    const std::scoped_lock<std::mutex> lock(m_stdinMutex);

    if (m_stdinWrite == invalidPipe())
    {
        return 0; // Pipe already closed.
    }

    return writePipe(m_stdinWrite, data.data(), data.size());
}

// Close the stdin pipe to signal EOF to the child process.
void IORedirect::closeStdin()
{
    const std::scoped_lock<std::mutex> lock(m_stdinMutex);
    closePipe(m_stdinWrite);
}

// Worker thread function: reads from a pipe and calls the output callback.
void IORedirect::readerWorker(StreamType type)
{
    // Select the appropriate pipe handle based on stream type.
    const bool isStdout = (type == StreamType::Stdout);
    const PipeHandle handle = isStdout ? m_stdoutRead : m_stderrRead;

    std::array<char, ReadBufferSize> buffer{};

    while (!m_stopWorkers.load())
    {
        // Read data from the pipe (blocking call).
        const int bytesRead = readPipe(handle, buffer.data(), buffer.size());

        if (bytesRead > 0)
        {
            // Deliver data to the callback.
            m_callback(type, gsl::span<char>(buffer.data(), bytesRead));
        }
        else if (bytesRead == 0)
        {
            // EOF: child process closed its end of the pipe.
            LOGD(log << "IORedirect: EOF on " << (isStdout ? "stdout" : "stderr"));
            break;
        }
        else
        {
            // Error reading from pipe.
            if (!m_stopWorkers.load())
            {
                LOGE(log << "IORedirect: read error on " << (isStdout ? "stdout" : "stderr")
                         << ", errno=" << errno);
            }
            break;
        }
    }
}

} // namespace dncdbg
