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

# How to add a new test

- move to the test-suite directory;

- create a new project in the test-suite folder:
```
    $ dotnet new console -o NewTest
```

- add a reference to the DbgTest library and `Context.cs` file to the `ItemGroup` section by editing the `NewTest.csproj` file, for example:
```
<Project Sdk="Microsoft.NET.Sdk">
  <ItemGroup>
    <ProjectReference Include="..\DbgTest\DbgTest.csproj" />
    <Compile Include="..\ScriptContext\Context.cs" />
  </ItemGroup>
  <PropertyGroup>
    <OutputType>Exe</OutputType>
    <TargetFramework>net10.0</TargetFramework>
    <Nullable>enable</Nullable>
  </PropertyGroup>
</Project>
```

- add the test name to the ALL_TEST_NAMES list in the `run_tests.sh` and `run_tests.ps1` scripts.
