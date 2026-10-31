#!/usr/bin/env python3
"""Native normalization versus the build Unicode database, including blocking."""
import ctypes
import json
import pathlib
import random
import subprocess
import tempfile
import unicodedata as ud

ROOT=pathlib.Path(__file__).resolve().parents[1]
def expected(text):
    normalized=ud.normalize("NFKC",text).translate(dict.fromkeys([0x622,0x623,0x625,0x671],0x627))
    return "".join(c for c in normalized if not ud.category(c).startswith("M")).encode()

def main():
    count=0
    with tempfile.TemporaryDirectory(prefix="niyah-unicode-") as tmp:
        lib=pathlib.Path(tmp)/"normalization.so"
        subprocess.run(["cc","-std=c11","-O2","-Wall","-Wextra","-Wpedantic","-Werror",
                        "-shared","-fPIC","-I"+str(ROOT/"src"),"-I"+str(ROOT/"build/tensor_core"),
                        str(ROOT/"src/tensor_normalize.c"),"-o",str(lib)],check=True)
        dll=ctypes.CDLL(str(lib));fn=dll.tensor_normalize
        fn.argtypes=[ctypes.c_char_p,ctypes.c_uint,ctypes.c_void_p,ctypes.c_uint,ctypes.POINTER(ctypes.c_uint)]
        dll.tensor_unicode_version.restype=ctypes.c_char_p
        assert dll.tensor_unicode_version().decode()==ud.unidata_version
        def check(text):
            nonlocal count
            source=text.encode();want=expected(text)
            if len(source)>128 or len(want)>512:return
            output=(ctypes.c_ubyte*528)(*([0xA5]*528));length=ctypes.c_uint(0xDEADBEEF)
            assert fn(source,len(source),output,512,ctypes.byref(length))==1,repr(text)
            assert length.value==len(want) and bytes(output[:len(want)])==want,(repr(text),bytes(output[:length.value]),want)
            assert bytes(output[len(want):])==b"\xA5"*(528-len(want))
            count+=1
        interesting=[]
        for cp in range(0x110000):
            if 0xD800<=cp<=0xDFFF:continue
            c=chr(cp)
            if (ud.normalize("NFKC",c)!=c or ud.combining(c) or ud.category(c).startswith("M")):
                check(c);interesting.append(c)
        for text in ["أَكَلَ","إِبْرَاهِيم","آدم","ٱلكتاب","ﻻ","ﷲ","ﻛﺘﺎﺏ",
                     "e\u0301\u0323","e\u0323\u0301","\u1100\u1161\u11a8",
                     "\u0301A\u030a","A\u0305\u030a","\u212b\u0327","والكتاب","سيكتب"]:
            check(text)
        rng=random.Random(20261028)
        pool=interesting+list("AaEexأإآٱبكتو")
        for _ in range(6000):check("".join(rng.choice(pool) for _ in range(rng.randrange(1,15))))
        for bad,cap in [(b"\xff",512),(b"\xc0\xaf",512),(b"\xed\xa0\x80",512),
                        (b"\xf4\x90\x80\x80",512),(b"a"*129,512),("ﻻ".encode(),1)]:
            output=(ctypes.c_ubyte*528)(*([0xA5]*528));length=ctypes.c_uint(0xDEADBEEF)
            assert fn(bad,len(bad),output,cap,ctypes.byref(length))==0
            assert bytes(output)==b"\xA5"*528 and length.value==0xDEADBEEF
            count+=1
    receipt={"status":"PASS","cases":count,"unicode_version":ud.unidata_version,
             "contract":"NFKC; four Alif variants unified; Unicode category M removed",
             "fertility_reduction_measured":False}
    out=ROOT/"build/tensor_core/normalization_receipt.json"
    out.parent.mkdir(parents=True,exist_ok=True);out.write_text(json.dumps(receipt,indent=2)+"\n")
    print(json.dumps(receipt))
if __name__=="__main__":main()
