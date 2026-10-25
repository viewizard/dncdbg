# How to launch tests locally

- On Linux and macOS:
```
    launch all tests:
    $ ./run_tests.sh
    or
    $ ./run_tests.sh <test-name> [<test-name>]
    or
    $ DNCDBG=<path-to-dncdbg> ./run_tests.sh <test-name> [<test-name>]
```

- On Windows:
```
    launch all tests:
    $ powershell.exe -executionpolicy bypass -File run_tests.ps1
    or
    $ powershell.exe -executionpolicy bypass -File run_tests.ps1 <test-name> [<test-name>]
```

The target framework can be overridden with the `DNCDBG_TARGET_FRAMEWORK` environment variable (for example, `net8.0`); by default, the tests target `net10.0`.

Tests are discovered automatically from the top-level directories whose names start with `Test`; no registration is required. Tests whose projects target a Windows-specific framework or use Windows Forms are skipped on Linux and macOS.

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
    <TargetFramework Condition="'$(DNCDBG_TARGET_FRAMEWORK)' != ''">$(DNCDBG_TARGET_FRAMEWORK)</TargetFramework>
    <TargetFramework Condition="'$(DNCDBG_TARGET_FRAMEWORK)' == ''">net10.0</TargetFramework>
    <Nullable>enable</Nullable>
  </PropertyGroup>

</Project>
```
