#!/usr/bin/env python3
import serial
import time
import re
import os.path
import os
import readline
import atexit
from esptool.cmds import detect_chip
import serial.serialutil

MAX_RETRIES = 5
MAX_READ_RETRIES = 1
PORT_NAME = "/dev/ttyACM"
BAUD_RATE = 115200
detectedPortName = None

histfile = os.path.join(os.path.expanduser("~"), ".pepit_fm_history")
try:
    readline.read_history_file(histfile)
    # default history len is -1 (infinite), which may grow unruly
    readline.set_history_length(1000)
except FileNotFoundError:
    pass

atexit.register(readline.write_history_file, histfile)

def connectSerial():
    global detectedPortName
    ser = None
    for i in range(10):
        try:
            ser = serial.Serial(f"{PORT_NAME}{i}", 115200, timeout=10)
            detectedPortName = f"{PORT_NAME}{i}"
            print(f"Port {PORT_NAME}{i} connected")
            break
        except serial.serialutil.SerialException:
            print(f"Port {PORT_NAME}{i} not found")
    return ser

ser = connectSerial()

def reset():
    global ser
    print("Resetting...")
    ser.close()
    with detect_chip(detectedPortName, connect_mode="usb_reset") as esp:
        esp.hard_reset()
    time.sleep(3)
    ser = connectSerial()

def writeFile(path, data):
    if isinstance(path,str):
        path = path.encode("utf-8")
    if isinstance(data,str):
        data = data.encode("utf-8")
    ser.flush()
    ser.reset_input_buffer()
    ser.reset_output_buffer()
    print(b" ul "+path)
    print(str(len(data)).encode("utf-8"))
    print(len(data))
    ser.write(b" ul "+path+b"\n")
    ser.write(str(len(data)).encode("utf-8")+b"\n")
    for i in range(1+len(data)):
        ser.write(data[i:i+1])
        ser.flush()
        if i % 1024 == 0:
            time.sleep(0.02)
    res = b""
    time.sleep(5)
    while ser.in_waiting > 0:
        res += ser.read(ser.in_waiting)
        time.sleep(0.01)
    print(res.decode("utf-8", errors="ignore"))

def readFile(path, retry=0):
    if retry>=MAX_RETRIES:
        print(f"Giving up downloading {path}")
        raise RuntimeError(f"Giving up downloading {path}")
    if isinstance(path,str):
        path = path.encode("utf-8")
    ser.flush()
    ser.reset_input_buffer()
    ser.reset_output_buffer()
    ser.write(b" catl "+path+b"\n")
    res = b""
    readRetry = 0
    while not res.endswith(b"Read from file: \r\n"):
        read = ser.read(1)
        if len(read) <= 0:
            readRetry += 1
            print("Nothing here, retrying")
        else:
            readRetry = 0
        if readRetry > MAX_READ_RETRIES:
            print(f"Failed to download {path} due to nothing to read. Retrying.")
            reset()
            return readFile(path, retry + 1)
        res += read
    lines = res.strip().splitlines()
    try:
        fileLen = int(lines[-2])
    except:
        print(f"Failed to download {path} due to wrong fileLen. Retrying.")
        reset()
        time.sleep(20)
        return readFile(path, retry + 1)
    print(f"File len: {fileLen}")
    out = ser.read(fileLen)
    readRetry = 0
    for i in range(3):
        if len(out)<fileLen:
            read = ser.read(fileLen - len(out))
            out += read
            if len(read) <= 0:
                readRetry += 1
                print("Nothing here, retrying")
            else:
                readRetry = 0
            if readRetry > MAX_READ_RETRIES:
                print(f"Failed to download {path} due to nothing to read. Retrying.")
                reset()
                return readFile(path, retry + 1)
    print(f"Bytes read: {len(out)}")
    if len(out) != fileLen:
        print(f"Failed to download {path}, retrying.")
        reset()
        return readFile(path, retry + 1)
    return out

def ls(path, absolute=False):
    if isinstance(path,str):
        path = path.encode("utf-8")
    if len(path)>1 and path[-1:]==b"/":
        path = path[:-1]
    ser.flush()
    ser.reset_input_buffer()
    ser.reset_output_buffer()
    ser.write(b" ls "+path+b"\n")
    res = b""
    while not res.endswith(b"\r\n$ "):
        res += ser.read(1)
    prefix = path if absolute else b""
    dirs  = [ os.path.join(prefix, x.strip()[6:]) for x in res.splitlines() if x.strip().startswith(b"DIR : ") ]
    files = [ tuple(x.strip()[6:].split(b" SIZE: ")) for x in res.splitlines() if x.strip().startswith(b"FILE: ") ]
    files = [ (os.path.join(prefix, x[0]), int(x[1])) for x in files ]
    return {"dirs":dirs, "files":files}

def leftPad(s, lm):
    l = lm - len(s)
    if (l<=0):
        return s
    return " "*l + s

def rightPad(s, lm):
    l = lm - len(s)
    if (l<=0):
        return s
    return s + " "*l

def printLs(lsData):
    print("DIRs:")
    print("\n".join([ "          "+x.decode("utf-8") for x in lsData["dirs"]]))

    print("Files:")
    print("\n".join([ leftPad(str(x[1]),8)+"  "+x[0].decode("utf-8") for x in lsData["files"]]))

def ul(src, target):
    with open(src.strip(), "rb") as f:
        data = f.read()
    writeFile(target.strip(), data)

def dl(src, target):
    data = readFile(src.strip())
    with open(target.strip(), "wb") as f:
        f.write(data)

def issueCommand(command, param):
    if isinstance(command,str):
        command = command.encode("utf-8")
    if isinstance(param,str):
        param = param.encode("utf-8")
    ser.flush()
    ser.reset_input_buffer()
    ser.reset_output_buffer()
    ser.write(b" "+command+b" "+param+b"\n")
    res = b""
    time.sleep(0.1)
    while ser.in_waiting > 0:
        res += ser.read(ser.in_waiting)
    return res

def mkdir(path):
    res = issueCommand("mkdir", path)
    if res:
        print(res.decode("utf-8"))

def rmdir(path):
    res = issueCommand("rmdir", path)
    if res:
        print(res.decode("utf-8"))

def rm(path):
    res = issueCommand("rm", path)
    if res:
        print(res.decode("utf-8"))

def rmdirr(path):
    lsres = ls(path)
    if isinstance(path, str):
        path = path.encode("utf-8")
    for file in lsres["files"]:
        rm(os.path.join(path, file[0]))
    for dir in lsres["dirs"]:
        rmdirr(os.path.join(path, dir))
        rmdir(os.path.join(path, dir))
    rmdir(path)

def ulr(src, target):
    print(f"ULR {src} {target}")
    paths = os.listdir(src)
    mkdir(target)
    for path in paths:
        srcPath = os.path.join(src, path)
        targetPath = os.path.join(target, path)
        if os.path.isdir(srcPath):
            ulr(srcPath, targetPath)
        elif os.path.isfile(srcPath):
            print(f"UL {srcPath} {targetPath}")
            ul(srcPath, targetPath)

def dlr(src, target):
    start = time.time()
    os.makedirs(target, exist_ok=True)
    print(f"DLR {src} {target}")
    lsres = ls(src)
    for file in lsres["files"]:
        filePath = file[0].decode("utf-8")
        print(f"DL {os.path.join(src, filePath)} {os.path.join(target, filePath)}")
        dl(os.path.join(src, filePath), os.path.join(target, filePath))
    for dir in lsres["dirs"]:
        dirPath = dir.decode("utf-8")
        dlr(os.path.join(src, dirPath), os.path.join(target, dirPath))
    print(f"Took {time.time()-start}s")

def checkDoublePath(param):
    path = param.split(" ")
    if len(path) == 1:
        return [
            path[0][1:],
            path[0]
        ]
    else:
        return [
            path[0][1:],
            path[1]
        ]

def main():
    if ("WIPSDCardContent" in os.getcwd()):
        localRoot = os.path.abspath(os.path.join(os.path.join(os.path.join(__file__, os.pardir), os.pardir), "WIPSDCardContent"))
    else:
        localRoot = os.path.abspath(os.path.join(os.path.join(os.path.join(__file__, os.pardir), os.pardir), "SDCardContent"))
    os.chdir(localRoot)
    print(f"Using root folder {localRoot}")
    pwd = "/"
    while True:
        inp = re.sub(" +", " ",input(f"{pwd} $ ")).strip()
        cmd = inp.split(" ")[0]
        param = inp[len(cmd):].strip()
        param = " ".join([ os.path.join(pwd, x) for x in param.split(" ") ])
        if cmd == "lsa":
            printLs(ls(param if param else pwd, True))
        elif cmd == "ls":
            printLs(ls(param if param else pwd, False))
        elif cmd == "lsl":
            print("\t ".join(os.listdir(os.path.join(localRoot, pwd[1:]))))
        elif cmd == "mkdir":
            mkdir(param)
        elif cmd == "rmdir":
            rmdir(param)
        elif cmd == "rmdirr":
            rmdirr(param)
        elif cmd == "rm":
            rm(param)
        elif cmd == "cat":
            res = readFile(param)
            res = res.decode("utf-8")
            print("File content:")
            print(res)
        elif cmd == "ul":
            path = checkDoublePath(param)
            ul(path[0], path[1])
        elif cmd == "ulr":
            path = checkDoublePath(param)
            print(path)
            ulr(path[0], path[1])
        elif cmd == "dl":
            path = checkDoublePath(param)
            dl(path[0], path[1])
        elif cmd == "dlr":
            path = checkDoublePath(param)
            print(path)
            dlr(path[0], path[1])
        elif cmd == "cd":
            if (param.startswith("/")):
                newPwd = param
            else:
                newPwd = os.path.abspath(os.path.join(pwd, param))
            if os.path.isdir(os.path.join(localRoot, newPwd[1:])):
                pwd = newPwd
                print(pwd)
            else:
                print(f"Path not found: {newPwd}")
        elif inp == "exit":
            exit()
        elif inp == "reset":
            reset()
        else:
            print(f"Unknown command: {inp}")


if __name__ == "__main__":
    main()
