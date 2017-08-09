"""
SerialTransporter.py

This module provides an easy to use python interface to execute tests on Test Driver.
It can be called from other python code as an API or can be executed in stand alone mode
to execute tests from command prompt. 

Followign example shows how to call it from python code::

    import SerialTransporter

    uut = L2PBufferStore(devices.WSS())
    uut.SwitchChannels()
    tc = TestCase(1234, uut)
    SerialTransporter.RunTestS(str(tc), "COM1")

When it is used as stand alone script it takes the low level protocol file as a 
command line parameter and send it to the Test Driver over serial port and 
captures the traces from the test execution.::

    Usage: python SerialTransporter.py COM1 test_data_file.txt
       example: python SerialTransporter.py COM1 TC_1025.txt

"""

import serial
import time
import threading
import Queue
import os
import sys
import re
import struct
import shutil
import zlib
import gzip


"""
IF auto compression is enabled and TXT file size > uncompressedTXTThreshold
it will be send to TestDriver in compressed format.

Any TXT file with size > uncompressedTXTMaxFileSizeThreshold will return error

Set here the uncompressedTXTThreshold limit
"""
enableAutoCompression = True
uncompressedTXTThreshold = 100 * 1024
uncompressedTXTMaxFileSizeThreshold = 100 * 1024 * 1024

class FileTooBigError(RuntimeError):
    pass
 
def GetFileLength(filePath):
    length = 0
    try:
        fileObject = open(filePath, 'rb')
        allData = fileObject.read()
        length = len(allData)
    finally:
        fileObject.close()
    return length

from serial.serialwin32 import * 
class MyWin32Serial(Win32Serial):
    """
    This class overrides the default SerialBase.readline() function
    default function read 1 character at a time that's not very efficient,
    and looses data (data overrun) while reading from serial port
    
    This version read data in chunks from serial port.  
    """
    def __init__(self, *args, **kwargs):
        Win32Serial.__init__(self, *args, **kwargs)
        self._line = ''
        self._readLineSize = 16
        
    def readline(self, size=None, eol='\n'):
        """
        Read a line which is terminated with end-of-line (eol) character
        ('backslash n' by default) or until size number of bytes or timeout
        """
        line = ''
        pos = self._line.find(eol)
        if pos != -1:
            if size is not None and pos > size:
                pos = size
            line = self._line[:pos+1]
            self._line = self._line[pos+1:] 
            return bytes(line)
        elif size is not None and len(self._line) >= size:
            line = self._line[:size+1]
            self._line = self._line[size+1:] 
            return bytes(line)

        if size is None:
            length = self._readLineSize
        else:
            length = size
        while 1:
            c = self.read(length)
            if c:
                pos = c.find(eol) 
                if pos == -1:
                    self._line += c
                    if size is not None:
                        length = length - len(c)
                        if length == 0:
                            line = self._line
                            self._line = ''
                            break
                else:
                    line = self._line + c[:pos+1]
                    self._line = c[pos+1:] 
                    break
            else:
                break
        return bytes(line)

class MySerial(MyWin32Serial, FileLike):
    pass

class DataCompressor:
    """
    This class provides functionality to compress data using gzip
    
    Compressed File Format:
            Magic (4 bytes)
            Compressed Size (4 bytes)
            Compressed Data CRC (4 bytes)
            Uncompressed Size (4 bytes)
            Uncompressed Data CRC (4 bytes)
            Data 
    """
    def __init__(self):
        pass
        
    def GetCompressionHeaderMagicWord(self):
        chars =  ['M','u','s','c']
        i = 24
        magicWord = 0
        for aChar in chars:
            aChar = ord(aChar) << i
            i -= 8
            magicWord |= aChar
        return magicWord 

    def CalculateCRC32(self, filePath):
        crc = zlib.crc32(open(filePath, 'rb').read())
        return crc

    def WrapCompressedDataWithHeader(self, compressedFileName, uncompressedFileName, littleEndian = False):
        """
        Compressed File Format:
                Magic (4 bytes)
                Compressed Size (4 bytes)
                Compressed Data CRC (4 bytes)
                Uncompressed Size (4 bytes)
                Uncompressed Data CRC (4 bytes)
                Data 
        """
        
        uncompressedSize = GetFileLength(uncompressedFileName)
        compressedSize = GetFileLength(compressedFileName)
        compressedDataCRC = self.CalculateCRC32(compressedFileName)
        uncompressedDataCRC = self.CalculateCRC32(uncompressedFileName)
        
        FileHeader = [
                     self.GetCompressionHeaderMagicWord(), 
                     compressedSize, 
                     compressedDataCRC,
                     uncompressedSize,
                     uncompressedDataCRC,
                     ]
        
        dataFormat = "<i"
        if not littleEndian:
            dataFormat = ">i"

        tempFilename = "t1.tmp"
        tempFile = open(tempFilename, "wb")
        for x in FileHeader:
            aWord = struct.pack(dataFormat, x)
            tempFile.write(aWord)
        
        compressedFile = open(compressedFileName, "rb")
        tempFile.write(compressedFile.read())
        compressedFile.close()
        tempFile.close()

        if os.path.exists(compressedFileName):
            os.remove(compressedFileName)
        shutil.move(tempFilename, compressedFileName)
        print "GZIP: compressed %d bytes into %d bytes" % (uncompressedSize, compressedSize)
                
    def Compress(self, inputFilename, outputFilename, littleEndian = False):
        """
        Compress using GZIP
        """
        fin = open(inputFilename, 'rb')
        fout = gzip.GzipFile(outputFilename, 'wb')
        fout.write(fin.read())
        fout.close()
        fin.close()
        self.WrapCompressedDataWithHeader(compressedFileName = outputFilename, uncompressedFileName = inputFilename, littleEndian = littleEndian)
        
    def UnCompress(self, inputFilename, outputFilename, littleEndian = False):
        """
        Uncompress using GZIP
        """
        try:
            fin = gzip.GzipFile(inputFilename, 'rb')
            fout = open(outputFilename, 'w')
            fout.write(fin.read())
            fout.close()
        except IOError as ex:
            raise Exception("UnCompress IOError")
        finally:
            fin.close()


mutex_uncomp = threading.Lock()
mutex_txt = threading.Lock()
mutex_exit = threading.Lock()

def threaded(fn):
    def wrapper(*args, **kwargs):
        threading.Thread(target=fn, args=args, kwargs=kwargs).start()
    return wrapper

class SerialSession:
    """
    It provides python API to interact to with the Test Driver using python code.
    It includes APIs to check if Test Driver is up and running, upload test data file, 
    upload firmware files and execute test on the Test Driver.
    """
    def __init__(self, sPort, filePath):
        self.filePath = filePath
        self.sPort = sPort
        self.firmwareFiles = {}
        self.allLines = open(filePath, 'rU').readlines()
        self.ExtractFirmwareFilesList()


        self.log_dir = self.filePath.replace('.txt', '')
        self.log_dir = self.log_dir.replace('.gzip', '')
        self.log_queue_comp = [] # queue for compressed log files, item only be added when it's relevant file has been generated.
        self.log_queue_txt_idx = 0 # an index to indicate the next uncompressed text log file
        self.exit = False # a variable to notify threads to exit

    def GetFirmwareFilePath(self, str):
        str = str.strip()
        pattern = r'.*FileOnPC="(.*)"\s+FirmwareFileNumber=(\d+)'
        match = re.match(pattern, str)
        allParts = []
        if match:
            allParts = match.groups()
            return [allParts[0], allParts[1]]
        return None

    def ExtractFirmwareFilesList(self):
        for aLine in self.allLines:
            if aLine.find('# DownloadFirmware') >= 0:
                frm = self.GetFirmwareFilePath(aLine)
                self.firmwareFiles[frm[1]] = frm[0]

    def UploadFirmwareFile(self, x, filePath):
        """
        Upload a firmware file to the Test Driver. Following steps are performed during upload::
        
            1. Send 'upload firmware'  
            2. Then send length of the file 
            3. Wait for test driver to send us - Transfer Firmware (xxxx) in Binary (xxxxxx) bytes
            4. Verify the firmware number and bytes expected by test driver are correct
            5. Send file data
            6. Wait for test driver to send us - File Loaded successfully
        
        """

        if not (os.path.isfile(filePath)):
            print "Error: File does not exist -", filePath
            sys.exit(1)
        fileLength = GetFileLength(filePath)
        if not fileLength:
            raise Exception("Firmware File zero length - %s" % self.filePath)
        
        str = 'upload firmware\n'
        self.sendData(str)
        time.sleep(1)
        data = self.sPort.readline()
        str = '%d\n' % fileLength
        self.sendData(str)
        time.sleep(1)

        pattern = r'.*Transfer Firmware (\d+) in Binary \((\d+) bytes.*'
        while True:
            data = self.sPort.readline()
            if len(data) > 0:
                print data,
                match = re.match(pattern, data)
                if match:
                    allParts = match.groups()
                    y1 = int(allParts[0])
                    y2 = int(allParts[1])
                    if (x != y1):
                        print "Error: expected firmware file number %d, instead got %d from device, Try resetting Test Driver" % (x, y1)
                        sys.exit(1)
                    if(y2 != fileLength):
                        print "Error: expected firmware file length %d, instead got %d" % (fileLength, y2)
                        sys.exit(1)
                    #print "We are ready to send the file now!!!"
                    try:
                        blockSize = 1024 * 8
                        fileObject = open(filePath, 'rb')
                        data = fileObject.read(blockSize)
                        while data and len(data) > 0:
                            self.sendData(data)
                            data = fileObject.read(blockSize)
                    finally:
                        fileObject.close()
                if data.find('File Loaded successfully') >= 0:
                    # Finish file upload to Test Driver
                    break
            
    def UploadFirmwareFiles(self):
        for x in range(len(self.firmwareFiles.keys())):
            print "Uploading Firmware to Test Driver - ", x + 1, self.firmwareFiles[str(x+1)]
            self.UploadFirmwareFile(x + 1, self.firmwareFiles[str(x+1)])
            
    def CalibrateTestDataFile(self):
        fileObject = open(self.filePath, 'r')
        data = fileObject.read()
        fileObject.close()
        
        fileObject = open(self.filePath, 'r')
        aLine = fileObject.readline()
        fileObject.close()
        aList = aLine.split(' ')
        tcid = aList[1]
        
        data = data.replace("T>BEGIN %s" % tcid, "T>BEGIN %sT>TLSCFG 1\n" % tcid)
        data = data.replace("T>END %s" % tcid, "T>TLSCFG 0\nT>END %s" % tcid)
        
        fileObject = open(self.filePath + "_", 'w')
        fileObject.write(data)
        fileObject.close()
        self.filePath = self.filePath + "_"
    
    def UploadTestDataFile(self, enableAutoCompression, uncompressedTXTThreshold):
        """
        Upload a test data file to the Test Driver. Following steps are performed during upload::
        
            1. Send 'upload testdata'  
            2. Then send length of the file 
            3. Wait for test driver to send us - Transfer Test Data File in Binary (xxxxxx) bytes
            4. Send file data
            5. Wait for test driver to send us - File Loaded successfully
        
        IF Auto Compression is enabled THEN
            any file with size > uncompressedTXTThreshold will be transferred in compressed format
            
        """
        fileLength = GetFileLength(self.filePath)
        if not fileLength:
            raise Exception("Test Data File zero length - %s" % self.filePath)
        
        if enableAutoCompression and fileLength > uncompressedTXTThreshold:
            zipper = DataCompressor()
            compressedFileName = self.filePath + ".gzip"
            zipper.Compress(inputFilename = self.filePath, outputFilename = compressedFileName)
            self.filePath = compressedFileName
            
        fileLength = GetFileLength(self.filePath)
        print "Uploading Test Data File to Test Driver -", self.filePath

        # ------ Firmware Download Procedure to Test Driver -------------
        # 1) Send 'upload testdata' 
        # 2) then send length of the file 
        # 3) Wait for card to send us - Transfer Test Data File in Binary (xxxxxx) bytes
        # 4)      - Verify the bytes expected by card are correct
        # 5) Send file data
        # 6) Wait for card to send us - File Loaded successfully
        
        str = 'upload testdata\n'
        self.sendData(str)
        time.sleep(1)
        str = '%d\n' % fileLength
        self.sendData(str)
        time.sleep(1)

        pattern = r'.*Transfer Test Data File in Binary \((\d+) bytes.*'
        while True:
            data = self.sPort.readline()
            if len(data) > 0:
                print data,
                match = re.match(pattern, data)
                if match:
                    allParts = match.groups()                    
                    y1 = int(allParts[0])
                    if(y1 != fileLength):
                        print "Error: expected test data file length %d, instead got from card %d" % (fileLength, y1)
                        sys.exit(1)
                    #print "We are ready to send the file now!!!"
                    
                    try:
                        blockSize = 1024 * 8
                        fileObject = open(self.filePath, 'rb')
                        data = fileObject.read(blockSize)
                        while data and len(data) > 0:
                            self.sendData(data)
                            data = fileObject.read(blockSize)
                    finally:
                        fileObject.close()
                    
                if data.find('File Loaded successfully') >= 0:
                    # Finish file upload to Test Driver, cleanup the temporary compressed file
                    if self.filePath.endswith(".gzip") and os.path.exists(self.filePath):
                        os.remove(self.filePath)
                    break

    def UploadAllFiles(self, enableAutoCompression, uncompressedTXTThreshold):
        self.UploadFirmwareFiles()
        #self.CalibrateTestDataFile()
        self.UploadTestDataFile(enableAutoCompression = enableAutoCompression, uncompressedTXTThreshold = uncompressedTXTThreshold)
    
    def UncompressTestResultFile(self, compressedFileName, textFileName):
        zipper = DataCompressor()
        zipper.UnCompress(inputFilename = compressedFileName, outputFilename = textFileName)
    
    def TestResultFileCalibration(self, tempFileName, compressedFileName):
        tempFile = open(tempFileName, 'rb')
        compFile = open(compressedFileName, 'wb')
        
        dat = tempFile.read()
        dat = list(dat)
        siz = len(dat)
        
        i = 0
        dat[siz:siz+1] = '\x00'
        while i < siz:
            if dat[i] == '\x0d' and dat[i+1] == '\x0a':
                pass
            else:
                compFile.write(dat[i])
            i = i + 1
        
        tempFile.close()
        compFile.close()

    @threaded
    def thread_uncomp(self):
        # uncompress compressed log files to text format
        while True:
            mutex_uncomp.acquire(1)
            if len(self.log_queue_comp) > 0:
                idx = self.log_queue_comp[0]
                self.log_queue_comp.remove(idx)

                #print "thread_uncomp idx = %d\n" % idx
            else:
                mutex_uncomp.release()
                
                mutex_exit.acquire(1)
                if self.exit is True:
                    mutex_exit.release()
                    break
                mutex_exit.release()
                
                time.sleep(1)
                continue
            mutex_uncomp.release()

            tempFile = self.log_dir + ".temp%d.txt.gz" % idx
            if not os.path.isfile(tempFile):
                continue
            compressedFile = self.log_dir + ".%d.txt.gz" % idx
            self.TestResultFileCalibration(tempFile, compressedFile)

            # compressed file crc check
            fileObject = open(compressedFile, 'rb')
            crc2 = zlib.crc32(fileObject.read())
            fileObject.close()
            #finfo.write("file %d crc = %08X\n" % (idx, (crc2 & 0xFFFFFFFF)))

            textFile = self.log_dir + ".%d.txt" % idx

            mutex_txt.acquire(1)
            try:
                self.UncompressTestResultFile(compressedFile, textFile)
            except IOError as ex:
                f = open(textFile, 'w')
                f.write("\nERROR: compressed log file crashed (%s).\n\n" % tempFile)
                f.close()
            mutex_txt.release()

            # delete temporary files
            try:
                if os.path.exists(tempFile):
                    os.remove(tempFile)
                if os.path.exists(compressedFile):
                    os.remove(compressedFile)
            except WindowsError as ex:
                # if files being used by another process, do nothing
                pass


    @threaded
    def thread_txt(self):
        while True:
            idx = self.log_queue_txt_idx
            textFile = self.log_dir + ".%d.txt" % idx

            mutex_txt.acquire(1)
            if os.path.exists(textFile):
                f = open(textFile, 'r')
                sys.stdout.write(f.read())
                #print "thread_txt idx = %d\n" % idx
                f.close()
            else:
                mutex_txt.release()
                
                mutex_exit.acquire(1)
                if self.exit is True:
                    mutex_exit.release()
                    break
                mutex_exit.release()
                
                time.sleep(1)
                continue
            mutex_txt.release()

            # delete temporary files
            if os.path.exists(textFile):
                try:
                    os.remove(textFile)
                except WindowsError as ex:
                    pass

            self.log_queue_txt_idx += 1

    def WaitToFinishTestExecution(self):
        """
        Triggers the test execution on the Test Driver and waits for the 
        test execution to finish. Following steps are performed::
         
            1. Send 'run'
            2. Keep reading all lines from Test Driver Serial Port 
            3. Wait for string "Test Execution Finished"
            
        """
        
        COMPRESSED_MODE_BEGIN = "***_ENTER_COMPRESSED_TRACE_LOG_MODE_***"
        COMPRESSED_MODE_END = "***_EXIT_COMPRESSED_TRACE_LOG_MODE_***"
        DATA_BLOCK_BEGIN = "***_DATA_BLOCK_BEGIN_***"
        DATA_BLOCK_END = "***_DATA_BLOCK_END_***"
        idx = 0
        buffer_size = 1024*8
        first_line_flag = 1 # 1: first text line not yet get 0: first text line get

        log_dir = self.log_dir
        info_log = log_dir + ".info.log"
        finfo = open(info_log, 'w')

        self.log_queue_comp = []
        self.log_queue_txt_idx = 0
        self.exit = False

        #--- Start threads for compressed log file uncompression and instant text file generation
        self.thread_uncomp()
        self.thread_txt()

        str = 'run\n'
        self.sendData(str)
        while True:
            data = self.sPort.readline()
            if len(data) > 0:
                if data.find(COMPRESSED_MODE_BEGIN) >= 0:
                    if first_line_flag == 0:
                        first_line_flag = 1
                        ftext.close()
                        mutex_txt.release()
                    finfo.write("idx = %d, mark = %s" % (idx, data))
                    
                    crc = self.sPort.readline()
                    finfo.write("crc = %s" % crc)
                    siz = self.sPort.readline()
                    finfo.write("len = %s" % siz)
                    siz = int(siz)
                    
                    fn = log_dir + ".temp%d.txt.gz" % idx
                    fileObject = open(fn, 'wb')
                    
                    if len(self.sPort._line) > siz:
                        dat = self.sPort._line[:siz]
                        self.sPort._line = self.sPort._line[siz:]
                    else:
                        dat = self.sPort._line
                        self.sPort._line = ''
                    cnt = len(dat)
                    while cnt < siz:
                        if siz - cnt > buffer_size:
                            read_size = buffer_size
                        else:
                            read_size = siz - cnt
                            
                        data = self.sPort.read(read_size)
                        cnt += len(data)
                        dat += data
                        
                    if cnt > siz:
                        finfo.write("extra data = %s\n" % dat[siz:])
                        dat = dat[:siz]
                    
                    fileObject.write(dat)
                    fileObject.close()

                    mutex_uncomp.acquire(1)
                    self.log_queue_comp.append(idx)
                    mutex_uncomp.release()

                    finfo.flush()
                    idx += 1
                else:
                    if data.find('Execution Finished') >= 0:
                        if first_line_flag == 0:
                            ftext.close()
                            mutex_txt.release()
                        finfo.write("get %s" % data)
                        break
                    
                    if first_line_flag == 1:
                        mutex_txt.acquire(1)
                        first_line_flag = 0
                        ftext = open(log_dir + ".%d.txt" % idx, 'w')
                        idx += 1
                    ftext.write(data)

        while self.log_queue_txt_idx < idx:
            time.sleep(1)

        mutex_exit.acquire(1)
        self.exit = True
        mutex_exit.release()
        
        sys.stdout.write(data) #print 'Execution Finished'
        finfo.close()
        os.remove(info_log)

    def VerifyTestDriverIsAlive(self):
        """
        Verify that Test Driver is alive 
        Following steps are performed::
         
            1. Send CR LF
            2. Wait for 'td>' string 
            
        """
        maxRetry = 30
        currentRetry = 0
        
        time.sleep(1)
        while True:
            currentRetry += 1
            if currentRetry > maxRetry:
                return False
            
            #--- Every 1 second hit ENTER
            self.sPort.timeout = 1
            self.sendData("\n\n")
            data = self.sPort.readline()
            if len(data) > 0:
                print data,
                if data.find('td>') >= 0:
                    return True
        
    def SetTestDriver_DebugModeON(self):
        """
        Sets the Test Driver in debug mode. This mode is used to debug issues in Test Driver.
        NOTE that it does not set the debug mode on UUT. When debug mode is ON in Test Driver
        extra information is printed in the logs, e.g. internal data structures of the Parser etc.
        """

        # ------ Test Execution Procedure on Test Driver -------------
        # 1) Send 'td debugmode'
        # 2) then wait for - Test Driver DEBUG Mode
        # 3) Send "1" to turn ON debug mode
        
        str = 'td debugmode\n'
        self.sendData(str)
        time.sleep(1)
        str = '1\n\n'
        self.sendData(str)
        time.sleep(1)
        while True:
            data = self.sPort.readline()
            if len(data) > 0:
                print data,
                if data.find('td>') >= 0:
                    break
        
    def SetTestDriver_TimeStampsOFF(self):
        """
        Turn OFF the timestamps on the logs generated by Test Driver.
        This can be useful when comparing the logs of two runs.
        """

        # ------ Test Execution Procedure on Test Driver -------------
        # 1) Send 'td timestamps'
        # 2) then wait for - Tracer
        # 3) Send "1" to turn OFF the time stamps on traces, 
        #        its helpful during development of Test Driver
        
        str = 'td timestamps\n'
        self.sendData(str)
        time.sleep(1)
        str = '1\n\n'
        self.sendData(str)
        time.sleep(1)
        while True:
            data = self.sPort.readline()
            if len(data) > 0:
                print data,
                if data.find('td>') >= 0:
                    break
    
    def SetTestDriver_CompressedModeON(self):
        # ------ Test Execution Procedure on Test Driver -------------
        # 1) Send 'T>TLSCFG 1'
        # 2) then wait for - 'td>'
        
        print self.sPort._line
        self.sPort._line = ''
        
        str = 'T>TLSCFG 1\n'
        self.sendData(str)
        time.sleep(1)
        while True:
            data = self.sPort.readline()
            if len(data) > 0:
                if data.find('td>') >= 0:
                    print 'T>TLSCFG 1\n'
                    break
                else:
                    time.sleep(1)
                    str = '\n'
                    self.sendData(str)
    
    def SetTestDriver_CompressedModeOFF(self):
        # ------ Test Execution Procedure on Test Driver -------------
        # 1) Send 'T>TLSCFG 0'
        # 2) then wait for - 'td>'
        
        print self.sPort._line
        self.sPort._line = ''
        
        str = 'T>TLSCFG 0\n'
        self.sendData(str)
        time.sleep(1)
        while True:
            data = self.sPort.readline()
            if len(data) > 0:
                if data.find('td>') >= 0:
                    print 'T>TLSCFG 0\n'
                    break
                else:
                    time.sleep(1)
                    str = '\n'
                    self.sendData(str)
    
    # Writes data to the Serial port
    def sendData(self, strCMD):
        x = self.sPort.write(strCMD)
        self.sPort.flush()
        #print "Bytes written to Serial Port = ", x

def RunTestF(inputFileName, serialPortNumber, TestDriverDebugMode = False, TestDriver_TimeStampsOFF = False, enableAutoCompression = True, uncompressedTXTThreshold = 100 * 1024):
    """
    Executes the test on the Test Driver. The following steps are performed::
    
        1. Connect to the Test Driver serial port
        2. Verify that Test Driver is alive by sending a CR LF and waiting for the 'td>' prompt string
        3. Upload the test data file to the Test Driver
        4. Upload all firmware files referenced in the test data to the Test Driver
        5. Trigger run
        6. Wait for execution to finish
    
    Arguments:
    
    * *inputFileName* - test data file (low level protocol file)
    * *serialPortNumber* - Serial port of Test Driver
      
    Optional Arguments:
    
    * *TestDriverDebugMode* - It can be True or False, default value is False. This can be used to run Test Driver in debug mode. When this option is on, Test Driver produces extra debug information related to internal data structures of the Test Driver including memory info, parser tables, execution lists etc.
    * *TestDriver_TimeStampsOFF* - It can be True or False, default value is False. When it is on, timestamps are turned off in log  
    """
    print "=" * 8, "PC-TO-TestDriver-Start", time.asctime(time.localtime(time.time())), "=" * 8

    if GetFileLength(inputFileName) > uncompressedTXTMaxFileSizeThreshold:
        raise FileTooBigError("File too big! TXT file size = %d, max allowed TXT file size = %d", GetFileLength(inputFileName), uncompressedTXTMaxFileSizeThreshold) 

    #--------------- Open Serial Port ----------------------
    try:
        sPort = MySerial(port=serialPortNumber, baudrate=921600, bytesize=8, parity='N', stopbits=1, xonxoff=0)
        sPort.timeout = 1
        sPort._readLineSize = 256
    except (Exception) as ex:
        print "Error -", ex
        sys.exit(1)
    
    session = SerialSession(sPort, inputFileName)
    if session.VerifyTestDriverIsAlive():
        if TestDriverDebugMode:
            session.SetTestDriver_DebugModeON()
        if TestDriver_TimeStampsOFF:
            session.SetTestDriver_TimeStampsOFF()
        session.SetTestDriver_CompressedModeON()
        session.UploadAllFiles(enableAutoCompression = enableAutoCompression, uncompressedTXTThreshold = uncompressedTXTThreshold)
        session.WaitToFinishTestExecution()
        #session.SetTestDriver_CompressedModeOFF()
    else:
        print "Error: TestDriver is not responding!"
    
    print "=" * 8, "PC-TO-TestDriver-End", time.asctime(time.localtime(time.time())), "=" * 8
    print '*'
    print '*'
    print '*'

def RunTestS(testDataString, serialPortNumber, TestDriverDebugMode = False, TestDriver_TimeStampsOFF = False, txtFile = None, logFile = None, enableAutoCompression = True, uncompressedTXTThreshold = 100 * 1024):
    """
    Executes the test on the Test Driver. The following steps are performed::
    
        1. Write the test data buffer to txtFile or 'temp.txt' file 
        2. Call RunTestF(txtFile or 'temp.txt', serialPortNumber, TestDriverDebugMode, TestDriver_TimeStampsOFF)
    
    Arguments:
    
    * *testDataString* - test data as a string buffer (low level protocol)
    * *serialPortNumber* - Serial port of Test Driver
      
    Optional Arguments:
    
    * *TestDriverDebugMode* - It can be True or False, default value is False. This can be used to run Test Driver in debug mode. When this option is on, Test Driver produces extra debug information related to internal data structures of the Test Driver including memory info, parser tables, execution lists etc.
    * *TestDriver_TimeStampsOFF* - It can be True or False, default value is False. When it is on, timestamps are turned off in log
    * *txtFile* - If the txt file name is provided, the test data will be stored in the txt file otherwise default name 'temp.txt' will be used
    * *logFile* - If the log file name is provided, the log will be stored in the log file otherwise log will be printed on the STDOUT (console)
      
    """
    tmpFileName = 'temp.txt'
    if txtFile:
        tmpFileName = txtFile
    fo = open(tmpFileName, "w")
    fo.writelines(testDataString)
    fo.close()
    
    if logFile:
        #--- Redirect the output from console (stdout) to logfile
        fout = open(logFile, "w")
        sys.stdout = fout 
    RunTestF(tmpFileName, serialPortNumber, TestDriverDebugMode, TestDriver_TimeStampsOFF, enableAutoCompression = enableAutoCompression, uncompressedTXTThreshold = uncompressedTXTThreshold)
    if logFile:
        #--- Restore the sys.stdout
        sys.stdout = sys.__stdout__
        if fout:
            fout.close()

##########################################################
# ------------------ Main Program -----------------------
#------
#--
#-

if __name__ == "__main__":
    # check the command line arguemnts
    if len(sys.argv) < 4:
        print "Please type Serial Test Data File as command line argument"
        print "Usage: python %s <COM-Port> <testfile.txt> <testfile.log>" % sys.argv[0]
        print "   examples: python %s COM1 serial-testdata-tc-SAB.txt output.log" % sys.argv[0]
        print "             python %s COM1 some-test.txt some-test.log" % sys.argv[0]
        sys.exit(1)
    
    # First argument is the name of the script itself
    # Second argument is the COM port
    # 3rd argument is the file name
    # 4th argument is the output file name
    serialPortNumber = sys.argv[1]
    inputFileName = sys.argv[2]
    outputFileName = sys.argv[3]
    TestDriverDebugMode = False
    TestDriver_TimeStampsOFF = False
    if (len(sys.argv) >= 5) and sys.argv[4]:
        TestDriverDebugMode = True
    if (len(sys.argv) >= 6) and sys.argv[5]:
        TestDriver_TimeStampsOFF = True
    
    #--- Redirect the output from console (stdout) to logfile
    fout = open(outputFileName, "w")
    sys.stdout = fout 

    RunTestF(inputFileName, serialPortNumber, TestDriverDebugMode, TestDriver_TimeStampsOFF, enableAutoCompression = enableAutoCompression, uncompressedTXTThreshold = uncompressedTXTThreshold)

    #--- Restore the sys.stdout
    sys.stdout = sys.__stdout__
    if fout:
        fout.close()
