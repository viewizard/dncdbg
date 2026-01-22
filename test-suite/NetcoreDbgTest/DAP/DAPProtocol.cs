using System;
using System.Collections.Generic;

namespace NetcoreDbgTest.DAP
{
// https://github.com/Microsoft/DAP-debugadapter-node/blob/master/debugProtocol.json
// https://github.com/Microsoft/DAP-debugadapter-node/blob/master/protocol/src/debugProtocol.ts
public class ProtocolMessage
{
    public int seq;
    public string type;
}
}
