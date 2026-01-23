# How to launch tests locally

- On Linux:
```
    launch all tests:
    $ ./run_tests.sh
    or
    $ ./run_tests.sh <test-name> [<test-name>]
    or
    $ DNCDBG=<path-to-netcoredbg> ./run_tests.sh <test-name> [<test-name>]
```

- On Windows:
```
    launch all tests:
    $ powershell.exe -executionpolicy bypass -File run_tests.ps1
    or
    $ powershell.exe -executionpolicy bypass -File run_tests.ps1 <test-name> [<test-name>]
```

# How to add new test

- move to test-suite directory;

- create new project in test-suite folder:
```
    $ dotnet new console -o NewTest
```

- add reference to NetcoreDbgTest library:
```
    $ dotnet add NewTest/NewTest.csproj reference NetcoreDbgTest/NetcoreDbgTest.csproj
```

- add test name into ALL_TEST_NAMES list in "run_tests.sh" and "run_tests.ps1" scripts;
