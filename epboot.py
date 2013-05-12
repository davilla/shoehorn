import serial
import struct

class epboot(object):
    def __init__(self,port):
        self.port=serial.Serial('/dev/ttyUSB0')
        self.port.baudrate=9600
    
    def enterboot(self):
        self.port.baudrate=9600
        print('Waiting for boot\n');
        while self.port.read(1)!=b'<':
            print('Other character got')
            pass
        a=open('loader.bin','rb')
        #a=open('dumpx.bin','rb')
        towrite=2048-self.port.write(a.read())
        self.port.write(towrite*b'\x00')
        print('Waiting for reply from BootROM')
        while self.port.read(1)!=b'>':
            print('Other character got')

    def readword(self,addr):
        self.port.flushInput()
        self.port.write(b'r'+struct.pack('I',addr))
        return struct.unpack('I',self.port.read(4))[0]

    def writeword(self,addr,data):
        self.port.flushInput()
        self.port.write(b'w'+struct.pack('II',addr,data))
        return
    
    def ping(self):
        self.port.flushInput()
        self.port.write(b'a')
        print('Waiting for reply from BootROM')
        while self.port.read(1)!=b'!':
            print('Other character got')
    
    def readblock(self,addr,length):
        self.port.flushInput()
        self.port.write(b'R'+struct.pack('II',addr,length))
        return self.port.read(length)



#writeword(port,0x80002300,0x422)

