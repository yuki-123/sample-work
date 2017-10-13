# -*- coding: utf-8 -*-
from selenium import webdriver
from selenium.webdriver.common.by import By
from selenium.webdriver.common.keys import Keys
from selenium.webdriver.support.ui import Select
from selenium.common.exceptions import NoSuchElementException
from selenium.common.exceptions import NoAlertPresentException
import unittest, time, re

#launches a web page in a browser and performs basic operations like send text, click on an element etc.

class Guitest():
    def __init__(self):
        self.path = "C:\\Users\\yuki\\chromedriver_win32\\chromedriver.exe"
        self.driver = webdriver.Chrome(self.path)
        self.driver.implicitly_wait(30)
        self.base_url = "http://www.google.com.au"
        self.verificationErrors = []
        self.accept_next_alert = True
        self.keyword = "qantas"
        self.departure = "canberra"
        self.destination = "sydney"
    
    def test_LandingPageTile(self):
        driver = self.driver
        driver.get(self.base_url)
        driver.implicitly_wait(30)
        driver.maximize_window()
        driver.find_element_by_name("q").send_keys(self.keyword)
        driver.find_element_by_name("q").submit()
        driver.find_element_by_id("vs0p1c0").click()
        assert(self.driver.title.startswith("Fly with Australia’s most popular airline | Qantas AU")) ,"PageTitle didn't match"


    def test_FlightPage(self):
        driver = self.driver
        driver.find_element_by_id("panel-book-a-trip").click()
        driver.find_element_by_id("typeahead-input-from").clear()
        driver.find_element_by_id("typeahead-input-from").send_keys(self.departure)
        driver.find_element_by_css_selector("#typeahead-list-item-from-0 > div > span").click()
        driver.find_element_by_id("typeahead-input-to").clear()
        driver.find_element_by_id("typeahead-input-to").send_keys(self.destination)
        driver.find_element_by_css_selector("#typeahead-list-item-to-0 > div > span").click()
        driver.find_element_by_xpath("(//button[@type='submit'])[4]").click()
        text = driver.find_element_by_xpath("//span[@class='h3 avail-option-summary-label']").text
        assert((text.lower()).find(self.departure) !=-1),"Unexpected departure"
        assert((text.lower()).find(self.destination) !=-1) ,"Unexpected destination "
    
    def StepEnd(self):
        self.driver.quit()

if __name__ == "__main__":
    gui = Guitest()
    gui.test_LandingPageTile()
    gui.test_FlightPage()
    gui.StepEnd()
