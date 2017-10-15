import requests 
import json
import re
import unittest
import sys
from pprint import pprint
import params

class Test():

    def __init__(self):
       self.url= ""
       self.expected_address = ""
       self.keyword =params.test2_param
       self.url1 = "http://maps.googleapis.com/maps/api/geocode/json"
       self.url2 ='https://en.wikipedia.org/w/index.php'
       self.params =params.test1_param
       self.location_index = 0 
       self.address_index = 1
       self.result =""
       self.testStarted = False
       self.test_no = 1
       self.testname = ""

    def TestStart(self):
        if self.testStarted:
            raise Exception('Test already started! It must end first!')
        self.WriteResult("Test %s  - Start test %d" % (self.testname,    self.test_no))
        self.testStarted = True
        
    def TestEnd(self):
        if not self.testStarted:
            raise Exception('Test not started yet! First call TestStart!')
        self.WriteResult("Test %s  - End test %d" % (self.testname,    self.test_no))
        self.testStarted = False
        self.test_no += 1

    def WriteResult(self, result):
        self.result = self.result + result + '\n'
 
    def WriteToRepot(self):
        file = open('TestResult.txt','a') 
        file.write(self.result)
        file.close()
        
    #  GET request test for google map API
    def test_example1(self):
        self.testname = "GET"
        self.TestStart()
        for param in self.params:
                self.expected_address = param[self.address_index]
                r = requests.get(url = self.url1, params = {'address':param[self.location_index]})
                data = r.json()
                formatted_address = data['results'][0]['formatted_address']
                if formatted_address != self.expected_address:
                    self.WriteResult("Test failed: Address didn't match address reurned is %s  expected %s" %(formatted_address, self.expected_address))
                else:
                    self.WriteResult("Test passed: Address matched %s" %(self.expected_address))
        self.TestEnd()
        
    # POST request test for wiki API           
    def test_example2(self):
        self.testname = "POST"
        self.TestStart()
        for key in self.keyword:
            req = requests.post(self.url2, data = {'search':'%s' %(key)})
            test  = 0
            for chunk in req.iter_content(chunk_size=10000):
                 if(re.search('"wgPageName":"%s","wgTitle":"%s"' % (key, key),  str(chunk))):
                        test = 1
            if test==1:
                 self.WriteResult("Test passed:page title, page name, and search string matched: %s" %(key))
            else:
                 self.WriteResult("Test faliled:page title, page name, and search string didn't match, check the spelling or refine your keyword: %s"  %(key))
        self.TestEnd()

if __name__ == "__main__":
    test =Test()
    test.test_example1()
    test.test_example2()
    test.WriteToRepot()


