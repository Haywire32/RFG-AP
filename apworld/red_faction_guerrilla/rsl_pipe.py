"""Length-framed game IPC, with bounded overlapped Windows I/O."""
import json
import struct
import win32con
import win32event
import win32file
import pywintypes
import ctypes
from ctypes import wintypes

kernel=ctypes.WinDLL('kernel32',use_last_error=True)
kernel.WaitNamedPipeW.argtypes=[wintypes.LPCWSTR,wintypes.DWORD]
kernel.WaitNamedPipeW.restype=wintypes.BOOL

class RslPipe:
    def __init__(self, name='RFGArchipelago'):
        self.name=name
        self.path='\\\\.\\pipe\\'+name
        self.fallback=None
        self.handle=None

    def select(self, name, fallback=None):
        if (name,fallback)!=(self.name,self.fallback):
            self.close()
            self.name=name
            self.path='\\\\.\\pipe\\'+name
            self.fallback=fallback

    def connect(self):
        path=self.path
        if not kernel.WaitNamedPipeW(path,1500):
            error=ctypes.get_last_error()
            # A busy/denied current runtime must never silently connect to a
            # different loader. Only older seeds can use the legacy endpoint.
            if error!=2 or not self.fallback:
                raise ctypes.WinError(error)
            path='\\\\.\\pipe\\'+self.fallback
            if not kernel.WaitNamedPipeW(path,1500):
                raise ctypes.WinError(ctypes.get_last_error())
        self.handle=win32file.CreateFile(path,win32con.GENERIC_READ|win32con.GENERIC_WRITE,
            0,None,win32con.OPEN_EXISTING,win32con.FILE_FLAG_OVERLAPPED,None)

    def close(self):
        if self.handle is not None:
            self.handle.Close()
            self.handle=None

    def transfer(self, data, reading=False):
        operation=pywintypes.OVERLAPPED()
        operation.hEvent=win32event.CreateEvent(None,True,False,None)
        try:
            if reading: status,buffer=win32file.ReadFile(self.handle,data,operation)
            else: status,_=win32file.WriteFile(self.handle,data,operation)
            if status not in (0,997): raise OSError(status,'Game pipe I/O failed')
            if status==997 and win32event.WaitForSingleObject(operation.hEvent,8000)!=win32event.WAIT_OBJECT_0:
                win32file.CancelIoEx(self.handle,operation)
                try: win32file.GetOverlappedResult(self.handle,operation,True)
                except pywintypes.error: pass
                raise TimeoutError('Game did not respond within 8 seconds')
            count=win32file.GetOverlappedResult(self.handle,operation,True)
            if count==0: raise ConnectionError('Game pipe closed')
            return bytes(buffer[:count]) if reading else count
        finally:
            operation.hEvent.Close()

    def send(self, snapshot):
        try:
            if self.handle is None:
                self.connect()
            body=json.dumps(snapshot,separators=(',',':')).encode()
            if len(body)>1048576: raise ValueError('Ownership snapshot too large')
            packet=struct.pack('<Iii',0x31475041,100,len(body))+body
            while packet: packet=packet[self.transfer(packet):]
            ack=b''
            while len(ack)<12: ack+=self.transfer(12-len(ack),True)
            if struct.unpack('<Iii',ack)!=(0x31415041,100,1):
                raise ValueError('Game rejected the state. Use the matching DLL and restart the game before changing seed.')
        except Exception:
            self.close()
            raise
