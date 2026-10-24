## Exception Conditions

Related to [VS Code Exception Conditions](https://code.visualstudio.com/docs/csharp/debugging#_exception-conditions)

The condition is a comma-separated list of exception types to break on; an exception type that starts with '!' is ignored. An exception type that ends with '*' is a wildcard that matches any exception type name with the same prefix. Negation takes priority over positive conditions regardless of their order. Note, DNCDbg has different exception condition rules.

#### Example conditions:

<div align="center">

<table>
  <thead>
    <tr>
      <th align="center">Example condition value</th>
      <th align="center">Result</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td align="left">System.NullReferenceException</td>
      <td align="left">This will only break on null reference exceptions.</td>
    </tr>
    <tr>
      <td align="left">System.NullReferenceException, System.InvalidOperationException</td>
      <td align="left">This will break on both null reference exceptions and invalid operation exceptions.</td>
    </tr>
    <tr>
      <td align="left">*, !System.NullReferenceException</td>
      <td align="left">This will break on all exceptions except null reference exceptions.</td>
    </tr>
    <tr>
      <td align="left">System.*, !System.NullReferenceException</td>
      <td align="left">This will break on all exceptions that match System.* except null reference exceptions.</td>
    </tr>
    <tr>
      <td align="left">System.*, !System.Null*</td>
      <td align="left">This will break on all exceptions that match System.* except exceptions that match System.Null*.</td>
    </tr>
    <tr>
      <td align="left">!*</td>
      <td align="left">This will ignore all exceptions.</td>
    </tr>
    <tr>
      <td align="left">System.*, !*</td>
      <td align="left">This will ignore all exceptions, since negation takes priority over positive conditions regardless of their order.</td>
    </tr>
  </tbody>
</table>

</div>

#### Mapping VsDbg conditions to DNCDbg conditions:

In VsDbg, if the condition starts with '!', the entire list is a list of exception types to ignore; DNCDbg negates each token individually, so such conditions are mapped with a lone '*' wildcard and per-token negation.

<div align="center">

<table>
  <thead>
    <tr>
      <th align="center">VsDbg conditions</th>
      <th align="center">DNCDbg conditions</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td align="left">System.NullReferenceException</td>
      <td align="left">System.NullReferenceException</td>
    </tr>
    <tr>
      <td align="left">System.NullReferenceException, System.InvalidOperationException</td>
      <td align="left">System.NullReferenceException, System.InvalidOperationException</td>
    </tr>
    <tr>
      <td align="left">!System.Threading.Tasks.TaskCanceledException</td>
      <td align="left">*, !System.Threading.Tasks.TaskCanceledException</td>
    </tr>
    <tr>
      <td align="left">!System.Threading.Tasks.TaskCanceledException, System.NotImplementedException</td>
      <td align="left">*, !System.Threading.Tasks.TaskCanceledException, !System.NotImplementedException</td>
    </tr>
  </tbody>
</table>

</div>