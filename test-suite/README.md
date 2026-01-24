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

- add reference to DNCDbgTest library and `Context.cs` file to `ItemGroup` section by edit `NewTest.csproj` file, for example:
```
<Project Sdk="Microsoft.NET.Sdk">
  <ItemGroup>
    <ProjectReference Include="..\DNCDbgTest\DNCDbgTest.csproj" />
    <Compile Include="..\ScriptContext\Context.cs" />
  </ItemGroup>
  <PropertyGroup>
    <OutputType>Exe</OutputType>
    <TargetFramework>net10.0</TargetFramework>
  </PropertyGroup>
</Project>
```

- add test name into ALL_TEST_NAMES list in `run_tests.sh` and `run_tests.ps1` scripts.
