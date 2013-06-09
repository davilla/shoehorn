import serial
import struct
import time
import sys

class epio(object):
    IO_ADDR=0x80000000
    SYSCON3=IO_ADDR+0x2200
    CLKCTL=0x6
    CLKCTL_73=0x6
    SDCONF=IO_ADDR+0x2300
    LEDFLSH=IO_ADDR+0x22C0
    UBRLCR1=IO_ADDR+0x04C0
    BRDIV=0xff
    BR_DICT={1200:191,2400:95,9600:23,19200:11,38400:5,57600:3,115200:1}
    MEMCFG1=IO_ADDR+0x180


class epboot(object):

    def __init__(self,port):
        self.port=serial.Serial('/dev/ttyUSB0')
        self.port.baudrate=9600
    
    def enterboot(self):
        self.port.baudrate=9600
        print('Waiting for boot');
        while self.port.read(1)!=b'<':
            print('Other character got')
            pass
        print('Got info from bootloader, sending loader.bin')
        a=open('loader.bin','rb')
        towrite=2048-self.port.write(a.read())
        self.port.write(towrite*b'\x00')
        print('Waiting for reply from BootROM')
        while self.port.read(1)!=b'>':
            print('Other character got')

    def readword(self,addr):
        self.port.flushInput()
        self.port.write(b'r'+struct.pack('I',addr))
        data=struct.unpack('I',self.port.read(4))[0]
        return data

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

    def writeblock(self,addr,data):
        self.port.flushInput()
        length=len(data)
        self.port.write(b'W'+struct.pack('II',addr,length))
        self.port.write(data)
        chksum=sum(data)&0xff
        gchksum=bytearray(self.port.read(1))[0]
        if (gchksum != chksum):
            raise IOError("Bad checksum got: %x expected: %x" % (gchksum,chksum))
        return

    def run(self,addr,reg0=0,reg1=0x5b,reg2=0,reg3=0):
        self.setbaud(9600)
        self.port.flushInput()
        self.port.write(b'c'+struct.pack('IIIII',addr,reg0,reg1,reg2,reg3))
        return

    def setbaud(self,baudrate):
        print("- Setting baudrate: "+ str(baudrate))
        data=(self.readword(epio.UBRLCR1) & ~epio.BRDIV) | epio.BR_DICT[baudrate]
        self.writeword(epio.UBRLCR1,data)
        time.sleep(0.1)
        #self.readword(epio.UBRLCR1)
        self.port.baudrate=baudrate
        self.port.flushInput()
        self.ping()
        return

    def inittracker(self):
        print("- flushing cache/TLB\n")
        self.port.write(b'4')
        self.ping()

        print("- 73MHz core clock")
        self.writeword(epio.SYSCON3,self.readword(epio.SYSCON3)& ~epio.CLKCTL |
                epio.CLKCTL_73)

        print("- SDRAM 64Mbit, CAS=2 W=16")
        self.writeword(epio.SDCONF,0x522)

        print("- Activate LED flasher")
        self.writeword(epio.LEDFLSH,0x40)

        print("- Setting up flash at CS0, 16 Bit, 3 Waitstate")
        self.writeword(epio.MEMCFG1,
                (self.readword(epio.MEMCFG1) &  0xffff0000) |
                                                0x00000014)
        self.setbaud(115200)

    def writefile(self,addr,filename):
        chunksize=0x1000
        fh=open(filename,'rb')
        fh.seek(0,2)
        filesize=fh.tell()
        fh.seek(0,0)
        while True:
            c=bytearray(fh.read(chunksize))
            if c==b'':
                break
            self.writeblock(addr,c)
            addr=addr+len(c)
            print('\r 0x%08x - 0x%08x - %d%%' %
                    (fh.tell(),filesize,int(fh.tell()*100/filesize))),
            sys.stdout.flush()
        fh.close()



#writeword(port,0x80002300,0x422)

