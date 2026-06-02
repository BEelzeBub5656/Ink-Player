"""Tests for the WeatherService using 和风天气 API."""

import os
from unittest.mock import patch, MagicMock, AsyncMock
import pytest
from services.weather_service import WeatherService


@pytest.fixture
def weather_service():
    """Create a WeatherService instance with a placeholder API key."""
    return WeatherService(api_key="QWEATHER_API_KEY", city_id="101010100")


@pytest.fixture
def mock_weather_response():
    """Simulate a successful 和风天气 /v7/weather/now response."""
    return {
        "code": "200",
        "now": {
            "temp": "22",
            "icon": "101",
            "text": "晴",
            "feelsLike": "21",
            "humidity": "45",
            "windDir": "南风",
            "windSpeed": "12"
        }
    }


@pytest.fixture
def mock_alerts_response():
    """Simulate a 和风天气 /v7/warning/now response."""
    return {
        "code": "200",
        "warning": [
            {
                "id": "W12345",
                "title": "北京市大风蓝色预警",
                "level": "蓝色",
                "type": "大风",
                "startTime": "2026-06-03T10:00:00+08:00",
                "endTime": "2026-06-04T10:00:00+08:00",
                "text": "预计3日夜间至4日白天，本市有4级左右偏北风，阵风7级左右。"
            }
        ]
    }


def _mock_response(response_data, status=200):
    """Create a mock aiohttp response object.

    The response object supports the async context manager protocol
    (__aenter__/__aexit__) as well as .json() being an async callable.
    """
    resp = AsyncMock()
    resp.status = status
    resp.json = AsyncMock(return_value=response_data)
    # __aenter__ returns self for aiohttp responses
    resp.__aenter__.return_value = resp
    return resp


class TestWeatherService:
    """Test suite for WeatherService."""

    def test_init_stores_api_key_and_city_id(self, weather_service):
        """Test that __init__ stores the provided values."""
        assert weather_service.api_key == "QWEATHER_API_KEY"
        assert weather_service.city_id == "101010100"

    def test_init_reads_api_key_from_env(self):
        """Test that API key defaults to env var QWEATHER_API_KEY."""
        os.environ["QWEATHER_API_KEY"] = "env_test_key_123"
        service = WeatherService(city_id="101010100")
        assert service.api_key == "env_test_key_123"
        del os.environ["QWEATHER_API_KEY"]

    @patch("services.weather_service.aiohttp.ClientSession")
    def test_check_weather_returns_correct_structure(self, mock_session_cls, weather_service, mock_weather_response):
        """Test that check_weather() returns the expected dict structure."""
        mock_resp = _mock_response(mock_weather_response)
        session_instance = MagicMock()
        session_instance.get.return_value = mock_resp
        mock_session_cls.return_value.__aenter__.return_value = session_instance

        import asyncio
        result = asyncio.run(weather_service.check_weather())

        assert isinstance(result, dict)
        assert "temp" in result
        assert "icon" in result
        assert "description" in result
        assert "alerts" in result
        assert result["temp"] == "22"
        assert result["icon"] == "101"
        assert result["description"] == "晴"

    @patch("services.weather_service.aiohttp.ClientSession")
    def test_check_alerts_returns_list(self, mock_session_cls, weather_service, mock_alerts_response):
        """Test that check_alerts() returns a list of alerts."""
        mock_resp = _mock_response(mock_alerts_response)
        session_instance = MagicMock()
        session_instance.get.return_value = mock_resp
        mock_session_cls.return_value.__aenter__.return_value = session_instance

        import asyncio
        result = asyncio.run(weather_service.check_alerts())

        assert isinstance(result, list)
        assert len(result) == 1
        assert result[0]["title"] == "北京市大风蓝色预警"
        assert result[0]["level"] == "蓝色"

    @patch("services.weather_service.aiohttp.ClientSession")
    def test_check_alerts_empty_when_no_alerts(self, mock_session_cls, weather_service):
        """Test that check_alerts() returns empty list when no alerts."""
        mock_resp = _mock_response({"code": "200", "warning": []})
        session_instance = MagicMock()
        session_instance.get.return_value = mock_resp
        mock_session_cls.return_value.__aenter__.return_value = session_instance

        import asyncio
        result = asyncio.run(weather_service.check_alerts())

        assert isinstance(result, list)
        assert len(result) == 0

    @patch("services.weather_service.aiohttp.ClientSession")
    def test_check_weather_with_alerts_included(self, mock_session_cls, weather_service, mock_weather_response, mock_alerts_response):
        """Test that check_weather() includes alerts when available."""
        mock_weather_resp = _mock_response(mock_weather_response)
        mock_alerts_resp = _mock_response(mock_alerts_response)

        session_instance = MagicMock()

        def get_side_effect(url, **kwargs):
            if "warning" in str(url):
                return mock_alerts_resp
            return mock_weather_resp

        session_instance.get.side_effect = get_side_effect
        mock_session_cls.return_value.__aenter__.return_value = session_instance

        import asyncio
        result = asyncio.run(weather_service.check_weather(include_alerts=True))

        assert result["alerts"] is not None
        assert len(result["alerts"]) == 1
        assert result["alerts"][0]["title"] == "北京市大风蓝色预警"
