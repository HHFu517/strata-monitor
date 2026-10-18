"""Private Windows host for the native launcher. Uses Strata's existing Python/runtime.
No HTTP control endpoint, console window, model edits, downloads or extra sampler.
"""
from __future__ import annotations
import argparse
import ctypes as C
from ctypes import wintypes as W
import json
import os
from pathlib import Path
import re
import socket
import struct
import sys
import threading
import time
import traceback
import _thread

K=C.WinDLL('kernel32',use_last_error=True)
def api(name,args,result):
    f=getattr(K,name);f.argtypes=args;f.restype=result;return f
api('OpenJobObjectW',[W.DWORD,W.BOOL,W.LPCWSTR],W.HANDLE)
api('OpenEventW',[W.DWORD,W.BOOL,W.LPCWSTR],W.HANDLE)
api('OpenProcess',[W.DWORD,W.BOOL,W.DWORD],W.HANDLE)
api('GetCurrentProcess',[],W.HANDLE)
api('GetProcessTimes',[W.HANDLE,C.POINTER(W.FILETIME),C.POINTER(W.FILETIME),C.POINTER(W.FILETIME),C.POINTER(W.FILETIME)],W.BOOL)
api('QueryFullProcessImageNameW',[W.HANDLE,W.DWORD,W.LPWSTR,C.POINTER(W.DWORD)],W.BOOL)
api('IsProcessInJob',[W.HANDLE,W.HANDLE,C.POINTER(W.BOOL)],W.BOOL)
api('WaitForSingleObject',[W.HANDLE,W.DWORD],W.DWORD)
api('WaitForMultipleObjects',[W.DWORD,C.POINTER(W.HANDLE),W.BOOL,W.DWORD],W.DWORD)
api('CreateMutexW',[C.c_void_p,W.BOOL,W.LPCWSTR],W.HANDLE)
api('ReleaseMutex',[W.HANDLE],W.BOOL)
api('SetEvent',[W.HANDLE],W.BOOL)
api('CloseHandle',[W.HANDLE],W.BOOL)

def norm(path):return os.path.normcase(os.path.abspath(path))
def read_json(path,limit=65536):
    if path.stat().st_size>limit:raise ValueError('文件过大')
    return json.loads(path.read_text(encoding='utf-8-sig'))
def atomic_json(path,value,run):
    temp=path.with_name(path.name+'.host-'+run)
    try:
        with temp.open('x',encoding='utf-8') as f:
            json.dump(value,f,ensure_ascii=False,indent=2);f.flush();os.fsync(f.fileno())
        for i in range(10):
            try:os.replace(temp,path);return
            except PermissionError:
                if i==9:raise
                time.sleep(.02)
    finally:
        try:temp.unlink()
        except FileNotFoundError:pass

def identity(handle):
    a,b,c,d=W.FILETIME(),W.FILETIME(),W.FILETIME(),W.FILETIME()
    if not K.GetProcessTimes(handle,C.byref(a),C.byref(b),C.byref(c),C.byref(d)):raise C.WinError(C.get_last_error())
    text=C.create_unicode_buffer(32768);n=W.DWORD(len(text))
    if not K.QueryFullProcessImageNameW(handle,0,text,C.byref(n)):raise C.WinError(C.get_last_error())
    return (a.dwHighDateTime<<32)+a.dwLowDateTime,text.value

def member(process,job):
    yes=W.BOOL();return bool(K.IsProcessInJob(process,job,C.byref(yes)) and yes.value)

def occupied(port):
    f=C.WinDLL('iphlpapi',use_last_error=True).GetExtendedTcpTable
    f.argtypes=[C.c_void_p,C.POINTER(W.ULONG),W.BOOL,W.ULONG,C.c_int,W.ULONG];f.restype=W.DWORD
    for family,row_size,port_offset in ((socket.AF_INET,24,8),(socket.AF_INET6,56,20)):
        size=W.ULONG();code=f(None,C.byref(size),False,family,3,0)
        if code==50 and family==socket.AF_INET6:continue
        if code not in (0,122):raise OSError(code,'无法确认端口状态')
        for _ in range(4):
            if size.value>16*1024*1024:raise ValueError('端口表过大')
            buf=C.create_string_buffer(max(size.value,16));code=f(buf,C.byref(size),False,family,3,0)
            if code!=122:break
        if code:raise OSError(code,'端口检查失败')
        count=struct.unpack_from('<I',buf.raw)[0]
        if 4+count*row_size>len(buf):raise ValueError('端口表格式异常')
        for i in range(count):
            value=struct.unpack_from('<I',buf.raw,4+i*row_size+port_offset)[0]
            if socket.ntohs(value&65535)==port:return True
    return False

class Entry(C.Structure):
    _fields_=[('dwSize',W.DWORD),('cntUsage',W.DWORD),('th32ProcessID',W.DWORD),('th32DefaultHeapID',C.c_size_t),('th32ModuleID',W.DWORD),('cntThreads',W.DWORD),('th32ParentProcessID',W.DWORD),('pcPriClassBase',W.LONG),('dwFlags',W.DWORD),('szExeFile',W.WCHAR*260)]
api('CreateToolhelp32Snapshot',[W.DWORD,W.DWORD],W.HANDLE)
api('Process32FirstW',[W.HANDLE,C.POINTER(Entry)],W.BOOL)
api('Process32NextW',[W.HANDLE,C.POINTER(Entry)],W.BOOL)
def engine_exists(name):
    h=K.CreateToolhelp32Snapshot(2,0)
    if not h or h==C.c_void_p(-1).value:raise C.WinError(C.get_last_error())
    try:
        e=Entry();e.dwSize=C.sizeof(e);more=K.Process32FirstW(h,C.byref(e))
        while more:
            if e.szExeFile.lower()==name.lower():return True
            more=K.Process32NextW(h,C.byref(e))
        return False
    finally:K.CloseHandle(h)

class RotatingText:
    encoding='utf-8';errors='replace';closed=False
    def __init__(self,path):
        self.path=path;self.lock=threading.RLock();self.null=os.open(os.devnull,os.O_WRONLY|os.O_BINARY)
        self.file=path.open('a',encoding='utf-8',buffering=1,newline='\n')
        self.size=path.stat().st_size
    def write(self,text):
        if not isinstance(text,str):text=str(text)
        length=len(text)
        data=text.encode('utf-8',errors='replace')
        if len(data)>1024*1024:text=data[-1024*1024:].decode('utf-8',errors='ignore')
        with self.lock:
            try:
                if self.size+len(text.encode('utf-8',errors='replace'))>2*1024*1024:
                    self.file.close()
                    try:os.replace(self.path,self.path.with_name(self.path.name+'.1'))
                    except OSError:
                        with self.path.open('w',encoding='utf-8'):pass
                    self.file=self.path.open('a',encoding='utf-8',buffering=1,newline='\n');self.size=0
                self.file.write(text);self.size+=len(text.encode('utf-8',errors='replace'))
            except OSError:pass # Diagnostics must not take down inference.
        return length
    def flush(self):
        with self.lock:
            try:self.file.flush()
            except OSError:pass
    def fileno(self):return self.null
    def isatty(self):return False
    def reconfigure(self,**kwargs):pass

def retain_logs(current):
    pattern=re.compile(r'startup-\d{8}-\d{6}-[0-9a-f]{32}\.log')
    others=[p for p in current.parent.iterdir() if p!=current and p.is_file() and pattern.fullmatch(p.name)]
    others.sort(key=lambda p:p.stat().st_mtime,reverse=True)
    for p in others[4:]:
        for x in (p,p.with_name(p.name+'.1')):
            try:x.unlink()
            except OSError:pass

def run(args):
    record_path=Path(args.record).absolute();record=read_json(record_path)
    if record.get('protocol')!=1 or record.get('run')!=args.run or not re.fullmatch('[0-9a-f]{32}',args.run):raise ValueError('启动记录不匹配')
    root=Path(record['root']).absolute();port=record['port'];preset=record['preset']
    if not isinstance(port,int) or not 0<port<=65535 or preset not in (0,1):raise ValueError('启动参数无效')
    expected_config=root/'app'/('strata-iq3_s.json' if preset else 'strata-iq3_xxs.json')
    if norm(record_path)!=norm(root/'monitor/run'/f'service-{port}.json') or norm(record['config'])!=norm(expected_config):raise ValueError('配置路径不匹配')
    if norm(record['host'])!=norm(__file__):raise ValueError('后台Host路径不匹配')
    log=Path(record['log']);pattern=r'startup-\d{8}-\d{6}-'+args.run+r'\.log'
    if norm(log.parent)!=norm(root/'monitor/logs/launcher') or not re.fullmatch(pattern,log.name):raise ValueError('日志路径不匹配')
    stream=RotatingText(log);sys.stdout=sys.stderr=stream;retain_logs(log)
    handles=[];owns_mutex=False;mutex=None;exit_code=1
    try:
        def opened(value):
            if not value:raise C.WinError(C.get_last_error())
            handles.append(value);return value
        job=opened(K.OpenJobObjectW(4,False,'Local\\StrataMonitor.Job.'+args.run))
        stop=opened(K.OpenEventW(0x100000,False,'Local\\StrataMonitor.Stop.'+args.run))
        ready=opened(K.OpenEventW(2,False,'Local\\StrataMonitor.Ready.'+args.run))
        browser=opened(K.OpenEventW(2,False,'Local\\StrataMonitor.Browser.'+args.run))
        parent=opened(K.OpenProcess(0x100000|0x1000,False,record['pid']))
        created,program=identity(parent)
        if created!=record['created'] or norm(program)!=norm(record['program']) or not member(parent,job) or not member(K.GetCurrentProcess(),job):raise ValueError('进程身份/Job验证失败')
        mutex_name=record['mutex']
        if not isinstance(mutex_name,str) or not mutex_name.startswith('Local\\Strata') or len(mutex_name)>200:raise ValueError('互斥锁名称无效')
        mutex=opened(K.CreateMutexW(None,False,mutex_name));code=K.WaitForSingleObject(mutex,0)
        if code not in (0,0x80):raise RuntimeError('另一个 Strata 正在启动或运行；未启动模型。')
        owns_mutex=True
        cfg=read_json(expected_config,256*1024)
        if cfg.get('host') not in ('127.0.0.1','localhost') or cfg.get('port')!=port or cfg.get('parallel',1)!=1:raise ValueError('仅支持配置中的本机单请求服务，端口必须一致。')
        engine=Path(cfg['exe'])
        if not engine.is_file():raise FileNotFoundError('引擎文件不存在')
        if occupied(port):raise RuntimeError('端口已被占用，未结束占用进程。')
        deadline=time.monotonic()+10
        while engine_exists(engine.name):
            if K.WaitForSingleObject(stop,0)==0:raise KeyboardInterrupt
            if time.monotonic()>=deadline:raise RuntimeError('仍有引擎在运行或释放资源，未启动第二个模型。')
            time.sleep(.25)
        if occupied(port):raise RuntimeError('检查期间端口被占用，未开始加载模型。')
        server=root/'app/serve/server.py'
        if not server.is_file():raise FileNotFoundError('服务脚本不存在')
        record.update(host_pid=os.getpid(),host_created=identity(K.GetCurrentProcess())[0],host_program=identity(K.GetCurrentProcess())[1])
        atomic_json(record_path,record,args.run)
        if not K.SetEvent(ready):raise C.WinError(C.get_last_error())
        def control():
            wait=(W.HANDLE*2)(stop,parent)
            result=K.WaitForMultipleObjects(2,wait,False,0xffffffff)
            if result==1:os._exit(1)
            if result==0:
                print('[launcher] stop requested',flush=True);_thread.interrupt_main()
                if K.WaitForSingleObject(parent,0xffffffff)==0:os._exit(1)
        threading.Thread(target=control,name='launcher-stop',daemon=True).start()
        import webbrowser
        def open_browser(url,*unused,**kwargs):
            if url not in (f'http://127.0.0.1:{port}/',f'http://localhost:{port}/'):return False
            return bool(K.SetEvent(browser)) # GUI opens it outside the model job.
        webbrowser.open=open_browser
        os.chdir(root/'app');sys.path.insert(0,str(root/'app'))
        sys.argv=[str(server),'--engine','strata','--config',str(expected_config),'--host',cfg['host'],'--port',str(port)]
        if cfg.get('open_browser',False):sys.argv.append('--open')
        if K.WaitForSingleObject(stop,0)==0:raise KeyboardInterrupt
        print('[launcher] starting existing Strata service; no console window',flush=True)
        import runpy
        try:runpy.run_path(str(server),run_name='__main__');exit_code=0
        except SystemExit as e:exit_code=e.code if isinstance(e.code,int) else (0 if e.code is None else 1)
    except KeyboardInterrupt:
        print('[launcher] startup canceled or service interrupted',flush=True);exit_code=0
    except BaseException as e:
        traceback.print_exc();print('[launcher] error: '+str(e),flush=True);exit_code=1
    finally:
        try:
            current=read_json(record_path)
            if current.get('run')==args.run:record_path.unlink()
        except (OSError,ValueError):pass
        print('[launcher] host exit '+str(exit_code),flush=True);stream.flush()
        if owns_mutex:K.ReleaseMutex(mutex)
        for h in reversed(handles):K.CloseHandle(h)
    os._exit(exit_code) # No orphan Python threads; the owned job cleans remaining children.

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--record',required=True);parser.add_argument('--run',required=True)
    try:run(parser.parse_args())
    except BaseException:
        traceback.print_exc();sys.stderr.flush();os._exit(1)
