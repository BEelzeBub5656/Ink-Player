"""Weather service using 和风天气 (QWeather) API.

Provides weather data including current conditions and weather alerts
for a given city using the QWeather API.
"""

import os
import aiohttp


class WeatherService:
    """Service for fetching weather data from 和风天气 API.

    Attributes:
        api_key: QWeather API key. Falls back to QWEATHER_API_KEY env var.
        city_id: City ID in QWeather format (e.g., "101010100" for Beijing).
        base_url: Base URL for the QWeather API.
    """

    def __init__(self, api_key: str = None, city_id: str = "101010100"):
        """Initialize the WeatherService.

        Args:
            api_key: QWeather API key. If None, reads from QWEATHER_API_KEY env.
            city_id: City ID for the location to query.
        """
        self.api_key = api_key or os.environ.get("QWEATHER_API_KEY", "")
        self.city_id = city_id
        self.base_url = "https://devapi.qweather.com/v7"

    async def check_weather(self, include_alerts: bool = False) -> dict:
        """Fetch current weather conditions.

        Args:
            include_alerts: If True, also fetches and includes weather alerts.

        Returns:
            dict with keys: temp, icon, description, alerts (list), plus raw data.
        """
        async with aiohttp.ClientSession() as session:
            url = f"{self.base_url}/weather/now"
            params = {"location": self.city_id, "key": self.api_key}
            async with session.get(url, params=params) as resp:
                if resp.status != 200:
                    return {"error": f"API returned status {resp.status}"}
                data = await resp.json()

        if data.get("code") != "200":
            return {"error": f"API error code: {data.get('code')}"}

        now = data.get("now", {})
        result = {
            "temp": now.get("temp", ""),
            "icon": now.get("icon", ""),
            "description": now.get("text", ""),
            "alerts": [],
        }

        if include_alerts:
            result["alerts"] = await self.check_alerts()

        return result

    async def check_alerts(self) -> list:
        """Fetch current weather alerts/warnings.

        Returns:
            list of alert dicts with keys: id, title, level, type, text, etc.
        """
        async with aiohttp.ClientSession() as session:
            url = f"{self.base_url}/warning/now"
            params = {"location": self.city_id, "key": self.api_key}
            async with session.get(url, params=params) as resp:
                if resp.status != 200:
                    return []
                data = await resp.json()

        if data.get("code") != "200":
            return []

        return data.get("warning", [])
