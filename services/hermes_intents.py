"""Hermes Agent intents for Ink-Player.

Defines the intent functions that the Ink-Player e-ink display can trigger
via the Hermes Agent framework. Each intent returns structured JSON suitable
for downstream processing.
"""


def set_timer(duration_minutes: int, message: str = "") -> dict:
    """Set a timer.

    Args:
        duration_minutes: Timer duration in minutes.
        message: Optional message to display when timer fires.

    Returns:
        dict: Structured timer intent with intent, duration, message, status.
    """
    return {
        "intent": "set_timer",
        "duration_minutes": duration_minutes,
        "message": message,
        "status": "scheduled",
    }


def set_reminder(time: str, message: str, date: str = "") -> dict:
    """Set a reminder at a specific time.

    Args:
        time: Time in HH:MM format.
        message: Reminder message.
        date: Optional date in YYYY-MM-DD format. Empty string means today.

    Returns:
        dict: Structured reminder intent.
    """
    return {
        "intent": "set_reminder",
        "time": time,
        "message": message,
        "date": date,
        "status": "scheduled",
    }


def weather_query(city: str = "") -> dict:
    """Query weather for a city.

    Args:
        city: City name to query weather for.

    Returns:
        dict: Structured weather query intent.
    """
    return {
        "intent": "weather_query",
        "city": city,
        "status": "pending",
        "service": "WeatherService",
    }


def schedule_query() -> list:
    """Query today's schedule.

    Returns:
        list: Schedule items with time, title, and description.
    """
    return [
        {
            "time": "09:00",
            "title": "晨会",
            "description": "团队日常站会",
        },
        {
            "time": "12:00",
            "title": "午餐",
            "description": "午休时间",
        },
        {
            "time": "14:00",
            "title": "项目评审",
            "description": "Ink-Player 项目进度评审",
        },
        {
            "time": "18:00",
            "title": "日报",
            "description": "填写工作日报",
        },
    ]


def web_search(query: str) -> dict:
    """Search the web.

    Args:
        query: Search query string.

    Returns:
        dict: Structured search intent with placeholder info.
    """
    return {
        "intent": "web_search",
        "query": query,
        "placeholder": "search placeholder, will use Firecrawl",
    }
