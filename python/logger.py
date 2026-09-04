from datetime import datetime


def write_log(zone_id, start_time, end_time):

    duration = round(
        (end_time - start_time).total_seconds(),
        2
    )

    log_text = (
        "----------------------------------------\n"
        f"Zone: {zone_id}\n"
        f"Enter Time: {start_time.strftime('%Y-%m-%d %H:%M:%S')}\n"
        f"Exit Time: {end_time.strftime('%Y-%m-%d %H:%M:%S')}\n"
        f"Duration: {duration} seconds\n"
        "----------------------------------------\n"
    )

    with open(
        "log.txt",
        "a",
        encoding="utf-8"
    ) as f:

        f.write(log_text)