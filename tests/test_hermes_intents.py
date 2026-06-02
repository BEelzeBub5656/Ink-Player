"""Tests for Hermes Agent intents registration."""

import json
import pytest
from services.hermes_intents import (
    set_timer,
    set_reminder,
    weather_query,
    schedule_query,
    web_search,
)


class TestSetTimer:
    """Tests for the set_timer intent."""

    def test_set_timer_returns_structured_json(self):
        """Test that set_timer returns a properly structured timer object."""
        result = set_timer(duration_minutes=5, message="Tea is ready")
        assert isinstance(result, dict)
        assert result["intent"] == "set_timer"
        assert result["duration_minutes"] == 5
        assert result["message"] == "Tea is ready"
        assert "status" in result
        assert result["status"] == "scheduled"

    def test_set_timer_default_message(self):
        """Test that set_timer works without a message."""
        result = set_timer(duration_minutes=10)
        assert result["duration_minutes"] == 10
        assert result["message"] == ""

    def test_set_timer_serializable(self):
        """Test that the result is JSON-serializable."""
        result = set_timer(duration_minutes=3, message="テスト")
        json_str = json.dumps(result, ensure_ascii=False)
        parsed = json.loads(json_str)
        assert parsed["duration_minutes"] == 3


class TestSetReminder:
    """Tests for the set_reminder intent."""

    def test_set_reminder_returns_structured_json(self):
        """Test that set_reminder returns a properly structured reminder."""
        result = set_reminder(
            time="14:30",
            message="Meeting with team",
            date="2026-06-03"
        )
        assert isinstance(result, dict)
        assert result["intent"] == "set_reminder"
        assert result["time"] == "14:30"
        assert result["message"] == "Meeting with team"
        assert result["date"] == "2026-06-03"
        assert result["status"] == "scheduled"

    def test_set_reminder_without_date(self):
        """Test that set_reminder works without a date (today)."""
        result = set_reminder(time="09:00", message="Wake up")
        assert result["date"] == ""

    def test_set_reminder_requires_time(self):
        """Test that set_reminder requires time."""
        with pytest.raises(TypeError):
            set_reminder(message="test")  # missing required time


class TestWeatherQuery:
    """Tests for the weather_query intent."""

    def test_weather_query_returns_structured_json(self):
        """Test that weather_query returns a properly structured query."""
        result = weather_query(city="北京")
        assert isinstance(result, dict)
        assert result["intent"] == "weather_query"
        assert result["city"] == "北京"
        assert result["status"] == "pending"
        assert "service" in result

    def test_weather_query_default_city(self):
        """Test that weather_query works with default city."""
        result = weather_query()
        assert result["city"] == ""


class TestScheduleQuery:
    """Tests for the schedule_query intent."""

    def test_schedule_query_returns_list(self):
        """Test that schedule_query returns a list of schedule items."""
        result = schedule_query()
        assert isinstance(result, list)
        assert len(result) > 0

    def test_schedule_items_have_required_fields(self):
        """Test that each schedule item has the required fields."""
        result = schedule_query()
        for item in result:
            assert "time" in item
            assert "title" in item
            assert "description" in item

    def test_schedule_query_serializable(self):
        """Test that the result is JSON-serializable."""
        result = schedule_query()
        json_str = json.dumps(result, ensure_ascii=False)
        parsed = json.loads(json_str)
        assert len(parsed) == len(result)


class TestWebSearch:
    """Tests for the web_search intent."""

    def test_web_search_returns_structured_json(self):
        """Test that web_search returns the placeholder structure."""
        result = web_search(query="Ink-Player setup guide")
        assert isinstance(result, dict)
        assert result["intent"] == "web_search"
        assert result["query"] == "Ink-Player setup guide"
        assert "placeholder" in result
        assert "Firecrawl" in result["placeholder"]

    def test_web_search_empty_query(self):
        """Test that web_search handles empty query."""
        result = web_search(query="")
        assert result["query"] == ""
