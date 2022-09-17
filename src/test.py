# radare - LGPL - Copyright 2022 - bemodtwz
import r2pipe

tests = [
    {
       "name" : "binint1",
       "asm" : """
            proto 1
            binint1 42
            binint1 43
            stop
       """,
       "ret" : '{"stack":[{"offset":4,"type":"PY_INT","value":43},{"offset":2,"type":"PY_INT","value":42}],"popstack":[],"memo":[]}\n'
    },
    {
       "name" : "pop",
       "asm" : """
            proto 2
            binint1 42
            pop
            stop
       """,
       "ret" : '{"stack":[],"popstack":[{"offset":2,"type":"PY_INT","value":42}],"memo":[]}\n'
    }, {
       "name" : "popmark",
       "asm" : """
            proto 2
            mark
            binint1 42
            binint1 43
            binint1 44
            binint1 45
            pop_mark
            stop
       """,
       "ret" : '{"stack":[],"popstack":[{"offset":9,"type":"PY_INT","value":45},{"offset":7,"type":"PY_INT","value":44},{"offset":5,"type":"PY_INT","value":43},{"offset":3,"type":"PY_INT","value":42}],"memo":[]}\n'
    }, {
       "name" : "bool",
       "asm" : """
            proto 2
            newtrue
            stop
       """,
       "ret" : '{"stack":[{"offset":2,"type":"PY_BOOL","value":true}],"popstack":[],"memo":[]}\n'
    }, {
       "name" : "setitems",
       # pickle.dump({"test_key" : True, "testkey2": False}, fp, protocol=2)
       "asm" : """
            proto 0x2
            empty_dict
            mark
            binunicode "test_key"
            newtrue
            binunicode "testkey2"
            newfalse
            setitems
            stop
       """,
       "ret" : '{"stack":[{"offset":2,"type":"PY_DICT","value":[[{"offset":4,"type":"PY_STR","value":"\\"test_key\\""},{"offset":17,"type":"PY_BOOL","value":true}],[{"offset":18,"type":"PY_STR","value":"\\"testkey2\\""},{"offset":31,"type":"PY_BOOL","value":false}]]}],"popstack":[],"memo":[]}\n'
    }, {
       "name" : "None",
       # pickle.dump({"test_key" : True, "testkey2": False}, fp, protocol=2)
       "asm" : """
            proto 0x2
            none
            stop
       """,
       "ret" : '{"stack":[{"offset":2,"type":"PY_NONE","value":null}],"popstack":[],"memo":[]}\n'
    }, {
       "name" : "List in memmo is the list in the stack",
       "asm" : """
            proto 2
            empty_list
            binput 1
            binint1 42
            append
            pop
            binget 1
            stop
       """,
       "ret" : '{"stack":[{"offset":2,"type":"PY_LIST","value":[{"offset":5,"type":"PY_INT","value":42}]}],"popstack":[{"offset":2,"type":"PY_LIST","value":[{"offset":5,"type":"PY_INT","value":42}]}],"memo":[{"index":1,"value":{"offset":2,"type":"PY_LIST","value":[{"offset":5,"type":"PY_INT","value":42}]}}]}\n'
    }, {
       "name" : "OP Float",
       "asm" : """
            proto 2
            float "1.2"
            stop
       """,
       "ret" : '{"stack":[{"offset":2,"type":"PY_FLOAT","value":1.200000}],"popstack":[],"memo":[]}\n'
    }, {
       "name" : "OP binfloat",
       "asm" : """
            proto 2
            binfloat 1.2
            stop
       """,
       "ret" : '{"stack":[{"offset":2,"type":"PY_FLOAT","value":1.200000}],"popstack":[],"memo":[]}\n'
    }, {
       "name" : "Many memos work",
       "asm" : """
            proto 2
            mark
            binint1 1
            binput 1
            binint1 2
            binput 2
            binint1 3
            binput 3
            binint1 4
            binput 4
            pop_mark
            binget 2
            binget 4
            stop
       """,
       "ret" : '{"stack":[{"offset":15,"type":"PY_INT","value":4},{"offset":7,"type":"PY_INT","value":2}],"popstack":[{"offset":15,"type":"PY_INT","value":4},{"offset":11,"type":"PY_INT","value":3},{"offset":7,"type":"PY_INT","value":2},{"offset":3,"type":"PY_INT","value":1}],"memo":[{"index":1,"value":{"offset":3,"type":"PY_INT","value":1}},{"index":2,"value":{"offset":7,"type":"PY_INT","value":2}},{"index":3,"value":{"offset":11,"type":"PY_INT","value":3}},{"index":4,"value":{"offset":15,"type":"PY_INT","value":4}}]}\n'
    },
]

def assemble_in_cache(r2, asm):
    s = asm.replace("\n", ";").replace("    ", "").replace('"', '\\"')
    if s[0] == ';': s = s[1:]
    r2.cmd('"wa %s"' % s)

r2 = r2pipe.open("-")
r2.cmd("e asm.arch = pickle")
# r2.cmd("e log.level = 5")
for i in tests:
    assemble_in_cache(r2, i["asm"])
    x = r2.cmd("pdPmj")
    if x == i["ret"]:
        print ("PASSED %s" % i["name"])
    else:
        print("FAILED %s" % i["name"])
        print("== got ==")
        print(repr(x))
        print("== SHOULD BE ==")
        print(repr(i["ret"]))
        break;