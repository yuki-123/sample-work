import requests 
import json
import re
import unittest
import sys
from pprint import pprint

class Test():

    def __init__(self):
       self.url= ""
       self.expected_address = "15 Broadway, Ultimo NSW 2007, Australia"
       self.param  = "University of Technology Sydney"
       self.keyword = "JSON"
       self.url1 = "http://maps.googleapis.com/maps/api/geocode/json"
       self.url2 ='https://en.wikipedia.org/w/index.php'

    #  simple test with GET           
    def test_example1(self):
        param  = {'address':self.param }
        r = requests.get(url = self.url1, params = param)
        data = r.json()
        formatted_address = data['results'][0]['formatted_address']
        if formatted_address != self.expected_address:
            print("Test1 failed: Address didn't match address reurned is %s  expected %d" %(formatted_address, self.expected_address) )
        else:
            print("Test1 passed: Address matched %s" %(self.expected_address))
        assert (formatted_address == self.expected_address), "Address didnt match"
        
    #  simple test with GET           
    def test_example2(self):
        req = requests.post(self.url2, data = {'search':'%s' %(self.keyword)})
        test  = 0
        for chunk in req.iter_content(chunk_size=10000):
             if(re.search('"wgPageName":"%s","wgTitle":"%s"' % (self.keyword, self.keyword),  str(chunk))):
                    test = 1
        if test==1:
            print ("Test2 passed:page title, page name, and search string matched: %s" %(self.keyword))
        else:
            print ("Test2 faliled:page title, page name, and search string didn't match, check the spelling or refine your keyword: %s"  %(self.keyword))
        assert (test), "Address didnt match"

if __name__ == "__main__":
    test =Test()
    test.test_example1()
    test.test_example2()




