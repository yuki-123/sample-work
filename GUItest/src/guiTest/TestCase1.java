package guiTest;

import java.util.concurrent.TimeUnit;

import org.openqa.selenium.WebDriver;
import org.openqa.selenium.chrome.ChromeDriver;
import org.openqa.selenium.edge.EdgeDriver;
import org.openqa.selenium.firefox.FirefoxDriver;

import org.openqa.selenium.support.PageFactory;

import org.testng.annotations.Test;

import guiTest.PageObject_Site1;

import org.testng.annotations.BeforeMethod;
import org.testng.annotations.BeforeSuite;
import org.testng.annotations.Parameters;
import org.testng.Assert;
import org.testng.annotations.AfterMethod;
import org.testng.annotations.AfterSuite;

public class TestCase1 extends Base {

	PageObject_Site1 pgb_site1;

	String url = "https://www.python.org/";
	String text;
	
  @BeforeSuite
  @Parameters({"browser"})
  public void beforeMethod(String browser) throws Exception {
	  
	  browserselection(browser);
	  browserNavigate(url);
	  pgb_site1 = new PageObject_Site1(driver);
	  pgb_site1 = PageFactory.initElements(driver, PageObject_Site1.class);

  }
  @Parameters({ "keyword1", "pythonVersion" })
  @Test
  public void test1(String keyword1, String pythonVersion) {

      text = pgb_site1.SelectSubMenu(pythonVersion);
	  Assert.assertEquals(pythonVersion,text);
  }

  @Parameters({"keyword2"})
  @Test
  public void test3(String keyword2) {

	  pgb_site1.GoToHomePage();
	  pgb_site1.SearchField(keyword2);
	  text = pgb_site1.GePageTitle();
	  Assert.assertTrue(text.toLowerCase().contains(keyword2.toLowerCase()), "Page title didn't match");
  }
 
  @AfterSuite
  public void afterSuite() {

	  driver.quit();

  }

}