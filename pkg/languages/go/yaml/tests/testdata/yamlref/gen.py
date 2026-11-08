import random, sys
seed = int(sys.argv[1]); nrand = int(sys.argv[2])
rnd = random.Random(seed)
S = []  # string scalars
Y = []  # yaml texts
F = []  # floats
words = ["true","True","TRUE","false","null","Null","~","","yes","no","on","off","Y","n","0","-0","+1","1_000","0x1F","0X1f","0o17","0O7","0b101",
 "-0b1","0b-1","-0o7","017","08","1e3","1E400","-1e400",".5",".5e3",".5_5","1.","+.inf",".inf",".NaN","-.Inf",".nan_","1_","_1",
 "2024-01-02","2024-1-2","2024-02-30","2023-02-29","2024-02-29","2024-01-02T03:04:05Z","2024-1-2t3:4:5.123+01:00","2024-1-2 3:4:5",
 "2024-01-02 03:04:05.999999999999","2024-01-02T03:04:05+24:00","2024-01-02T03:04:05+25:00","2024-13-01","20240-1-1","2024-01-02x",
 "9223372036854775807","9223372036854775808","18446744073709551615","18446744073709551616","-9223372036854775808","-9223372036854775809",
 "<<","=","- a","-a","-","--","---","...","....","--- a","? a","?a",": a",":a","a:","a: b","a:b","a #b","a#b","#a","a ,b","a,b","[a]","{a}","a]","&a","*a","!a","|a",">a","'a","\"a","%a","@a","`a",
 " a","a "," ","  ","\t","a\tb","a\nb","a\n","\na","a\n\nb","a \nb","a\n b","a\r\nb","a\rb","\u0085","a\u2028b","a\u2029b","\u00a0","\ufeffa","a\ufeff",
 "\x00","a\x00b","\x07","\x1b","\x7f","\u0080","\u009f","\ud7ff","\ue000","\ufffd","\ufffe","\U0001F600","é","日本語","a\\b","a\"b","a'b","a''b",
 "https://example.com/a?b=c#d","#/components/schemas/Foo","^-?(?:0|[1-9][0-9]{0,11})(?:\\.[0-9]{1,9})?s$","{id}","a: b: c","key: value # c",
 "The `Status` type: errors. And more: here","x" * 200]
long_words = ["lorem","ipsum","dolor","sit","amet,","consectetur","adipiscing","elit:","sed","do","#tag","- item","[x]","'q'","\"d\"","a\tb","  ","\n","\n\n"]
def longtext():
    n = rnd.randint(5, 60)
    return " ".join(rnd.choice(long_words) for _ in range(n))
S += words
for w in words[:80]:
    S.append(w + " " + w)
    S.append("x " + w)
for _ in range(60):
    S.append(longtext())
Y += ["red","green","RED","TRUE","NULL","null","~","1","0x10","1.5","2024-01-02","'quoted'","\"dq\"","\"a\\u00e9\\x41\\U0001F600\"","{\"id\": \"1\", \"text\": \"hello\"}",
 "{a: 1, b: [1, 2, {c: d}]}","[1, 2, 3]","[]","{}","a: 1\nb: 2\n","- a\n- b\n","- a\n-  - b\n   - c\n","a:\n  - x\n  - y\nb: {c: d}\n",
 "# head\na: 1 # line\n# foot\n","a: 1\n\n# foot\n\nb: 2\n","# doc head\n\n# key head\na: 1\n","- # c\n  - x\n","a: &x 1\nb: *x\n","a: *nope\n",
 "!!str 1","!!int 1","!!float 1","!foo bar","!<tag:yaml.org,2002:str> x","%YAML 1.1\n---\na\n","%TAG !e! tag:example.com,2000:\n--- !e!foo x\n",
 "--- a\n--- b\n","---\n...\n","","   ","\n\n","# only comment\n","a\n# c\n","|\n  lit\n  eral\n","|-\n  x\n",">\n  fold\n  ed\n\n  p\n",">+\n  x\n\n","|2\n   x\n",
 "'it''s'","\"a\nb\"","'a\n\n  b'","a: b: c","[a, b","{a: b","- a\nb: c","\t a","a:\tb","\"\\q\"","\"\\x4\"","x: \"\\ud800\"","? a\n: b\n","? [a]\n: {b: c}\n",
 "{? a : b}","[a: b]","[? a : b, c]","a: |\n  x\n  y\nb: 1\n","- >-\n  x\n  y\n","key:    value    ","\ufeffa: 1","a: 1\r\nb: 2\r\n","a\x00","a\x01b","\x85",
 "{a: [b, {c: [d, {e: f}]}]}", "- [a, b]\n- {c: d}\n", "a:\n  b:\n    c:\n      d: e\n", "a: 'x' # c\nb: \"y\" # d\n", "[a, # c\n b]", "{a: 1, # c\n b: 2}"]
for _ in range(80):
    parts = []
    for _ in range(rnd.randint(1, 6)):
        parts.append(rnd.choice(["a", "b: c", "- d", "  ", "\n", "# x", "[", "]", "{", "}", ",", ":", "?", "'q'", "\"r\"", "|", ">", "&n", "*n", "!t", "---", "...", "\t", "é", "1", "true", "~"]))
    Y.append("".join(parts))
F += ["0","1","-1","10","5","1e6","1e21","1e20","123456","1234567","0.0001","0.00001","1.5","-0","NaN","Inf","-Inf","3.14159","1e-7","255","1e300","4.9e-324"]
cases = [("s", s) for s in S] + [("y", y) for y in Y] + [("f", f) for f in F]
# random extra cases
alphabet = ["a","b"," ","  ","\n","\t","#",":","-","?",",","[","]","{","}","'","\"","\\","!","&","*","|",">","%","@","`","é","\u2028","\x85","\x00","\r","1","0x","e","."]
for _ in range(nrand):
    s = "".join(rnd.choice(alphabet) for _ in range(rnd.randint(0, 25)))
    cases.append((rnd.choice("sy"), s))
for m, s in cases:
    print(m, s.encode("utf-8", "surrogatepass").hex())
