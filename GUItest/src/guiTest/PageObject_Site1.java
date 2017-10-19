package guiTest;

import org.openqa.selenium.WebDriver;

import org.openqa.selenium.WebElement;

import org.openqa.selenium.support.FindBy;

import org.openqa.selenium.support.How;
import org.testng.Assert;

import java.util.concurrent.TimeUnit;
import org.openqa.selenium.By;

import org.openqa.selenium.chrome.ChromeDriver;
import org.openqa.selenium.edge.EdgeDriver;
import org.openqa.selenium.firefox.FirefoxDriver;
import org.testng.Assert;
import org.testng.annotations.BeforeTest;
import org.testng.annotations.Parameters;
import org.testng.annotations.Test;
import org.openqa.selenium.support.PageFactory;

public class PageObject_Site1 {

	final WebDriver driver;

	WebElement searchengine;
	WebElement btn;
	WebElement pythonLink;
	WebElement downloadLink;
	WebElement versionLink;
	WebElement pythonHome;
	WebElement title;
	WebElement searchBox;
	WebElement submitBtn;
	WebElement mailingLink;
	WebElement resultLink;
	
    String text;

	public PageObject_Site1(WebDriver driver)
	{
		this.driver = driver;
	}
	
	public String SelectSubMenu(String pythonVersion)
	{
		downloadLink = driver.findElement(By.linkText("All releases"));
		downloadLink.click();
		versionLink = driver.findElement(By.linkText(pythonVersion));
		versionLink.click();
	    text = driver.findElement(By.className("page-title")).getText();
	    
		return text;
	}
	
	public void GoToHomePage()
	{
		pythonHome = driver.findElement(By.cssSelector("img.python-logo"));
		pythonHome.click();
	}
	
	public void  SearchField(String keyword)
	{
		
		searchBox = driver.findElement(By.id("id-search-field"));
		searchBox.clear();
		searchBox.sendKeys(keyword);
		submitBtn = driver.findElement(By.id("submit"));
		submitBtn.click();
	}  
	public String  GePageTitle()
	{    
		mailingLink = driver.findElement(By.xpath("(//a[contains(text(),'Mailing Lists')])[3]"));
		mailingLink.click();
	    driver.findElement(By.tagName("title"));
	    resultLink = driver.findElement(By.cssSelector("a.reference.external"));
	    resultLink.click();
	    text = driver.findElement(By.tagName("h2")).getText();
	    
	    return text;
	 }
	public String GetPageText()
	{

		text = driver.findElement(By.xpath("//p[contains(text(),'fond')]")).getText();
		return text;
		
		
	}

	}

