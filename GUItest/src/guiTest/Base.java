package guiTest;

import java.util.concurrent.TimeUnit;

import org.openqa.selenium.WebDriver;
import org.openqa.selenium.chrome.ChromeDriver;
import org.openqa.selenium.edge.EdgeDriver;
import org.openqa.selenium.firefox.FirefoxDriver;

public class Base 
{
	static WebDriver driver;
	
	public static void browserselection(String browser)
	{

		if(browser.equalsIgnoreCase("firefox"))
		{
			
			System.setProperty("webdriver.firefox.marionette", "D:\\Selenium\\geckodriver-v0.18.0-win32\\geckodriver.exe");
			driver = new FirefoxDriver();
		}
		else if(browser.equalsIgnoreCase("chrome"))
		{
			System.setProperty("webdriver.chrome.driver","D:\\Selenium\\chromedriver_win32\\chromedriver.exe");
			driver = new ChromeDriver();
		}
				else if(browser.equalsIgnoreCase("Edge"))
				{
					System.setProperty("webdriver.edge.driver","D:\\Selenium\\chromedriver_win32\\MicrosoftWebDriver.exe");
					driver = new EdgeDriver();
				}
		else{

		}
		driver.manage().timeouts().implicitlyWait(10, TimeUnit.SECONDS);
		
	}
	
	public static void browserNavigate(String url) throws Exception
	{
		driver.manage().window().maximize();
		driver.get(url);
		driver.manage().timeouts().implicitlyWait(30, TimeUnit.SECONDS);
	}

	
}
