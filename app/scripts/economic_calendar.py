from selenium import webdriver
from selenium.webdriver.chrome.options import Options
from selenium.webdriver.common.by import By
from selenium.webdriver.support.ui import WebDriverWait
from selenium.webdriver.support import expected_conditions as EC
import json
from datetime import datetime
import time

def scrape_forex_calendar(date_str="oct15.2026"):
    """
    Scrapes Forex Factory calendar for given date
    Args:
        date_str: Date in format like 'oct15.2026'
    Returns:
        JSON with structured data
    """
    url = f"https://www.forexfactory.com/calendar?day={date_str}"

    # Setup headless Chrome
    options = Options()
    options.add_argument('--headless')
    options.add_argument('--no-sandbox')
    options.add_argument('--disable-dev-shm-usage')
    options.add_argument('--user-agent=Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36')

    driver = webdriver.Chrome(options=options)
    events = []

    try:
        driver.get(url)

        # Wait for calendar table to load
        wait = WebDriverWait(driver, 10)
        wait.until(EC.presence_of_element_located((By.CLASS_NAME, "calendar__table")))

        time.sleep(2)  # Allow dynamic content to load

        # Find all calendar rows
        rows = driver.find_elements(By.CSS_SELECTOR, "tr.calendar__row")

        for row in rows:
            try:
                event = {}

                # Time
                time_elem = row.find_elements(By.CLASS_NAME, "calendar__time")
                event['time'] = time_elem[0].text if time_elem else ""

                # Currency
                currency_elem = row.find_elements(By.CLASS_NAME, "calendar__currency")
                event['currency'] = currency_elem[0].text if currency_elem else ""

                # Impact
                impact_elem = row.find_elements(By.CLASS_NAME, "calendar__impact")
                if impact_elem:
                    impact_spans = impact_elem[0].find_elements(By.TAG_NAME, "span")
                    event['impact'] = len([s for s in impact_spans if 'icon--ff-impact-red' in s.get_attribute('class') or 'icon--ff-impact-ora' in s.get_attribute('class') or 'icon--ff-impact-yel' in s.get_attribute('class')])
                else:
                    event['impact'] = 0

                # Event name
                event_elem = row.find_elements(By.CLASS_NAME, "calendar__event")
                event['event'] = event_elem[0].text.strip() if event_elem else ""

                # Actual value
                actual_elem = row.find_elements(By.CLASS_NAME, "calendar__actual")
                event['actual'] = actual_elem[0].text if actual_elem else ""

                # Forecast value
                forecast_elem = row.find_elements(By.CLASS_NAME, "calendar__forecast")
                event['forecast'] = forecast_elem[0].text if forecast_elem else ""

                # Previous value
                previous_elem = row.find_elements(By.CLASS_NAME, "calendar__previous")
                event['previous'] = previous_elem[0].text if previous_elem else ""

                # Only add if event has meaningful data
                if event['event'] and event['event'] not in ['', 'All Day']:
                    events.append(event)

            except Exception as e:
                continue

    finally:
        driver.quit()

    return {
        "date": date_str,
        "url": url,
        "events_count": len(events),
        "events": events
    }

if __name__ == "__main__":
    import sys

    # The Economics calendar panel passes the day as Forex Factory writes it
    # ("oct16.2026"); with none, today.
    if len(sys.argv) > 1:
        day = sys.argv[1]
    else:
        today = datetime.now()
        day = f"{today.strftime('%b').lower()}{today.day}.{today.year}"
    data = scrape_forex_calendar(day)

    print(json.dumps(data, indent=2))