// Wall panel calendar feed (Google Apps Script).
//
// Returns WEEKS weeks of events, starting on the Monday of the week that
// holds the 1st of this month (so the panel's month view is complete), from
// every calendar that is ticked in Google Calendar, as a small JSON list.
// Recurring events are already expanded by CalendarApp.
//
// Setup:
// 1. Go to https://script.google.com, New project, paste this file.
// 2. Change TOKEN below to a long random string.
// 3. Deploy > New deployment > type "Web app".
//    Execute as: Me. Who has access: Anyone.
// 4. Authorize when asked, then copy the Web app URL.
// 5. On the panel's settings page, paste: <Web app URL>?token=<TOKEN>
//
// Anyone with the URL and token can read your events, so keep both private.

const TOKEN = 'change-me-to-a-long-random-string';
const WEEKS = 10;
const MAX_EVENTS = 200;

function doGet(e) {
  if ((e.parameter.token || '') !== TOKEN) {
    return json({error: 'bad token'});
  }

  const now = new Date();
  const first = new Date(now.getFullYear(), now.getMonth(), 1);
  const back = (first.getDay() + 6) % 7; // days since Monday
  const start = new Date(first.getFullYear(), first.getMonth(), 1 - back);
  const end = new Date(start.getFullYear(), start.getMonth(), start.getDate() + WEEKS * 7);

  const events = [];
  CalendarApp.getAllCalendars()
    .filter(cal => cal.isSelected())
    .forEach(cal => {
      const colour = (cal.getColor() || '#7a8bff').replace('#', '');
      cal.getEvents(start, end).forEach(ev => {
        events.push({
          s: Math.floor(ev.getStartTime().getTime() / 1000),
          e: Math.floor(ev.getEndTime().getTime() / 1000),
          a: ev.isAllDayEvent() ? 1 : 0,
          t: ev.getTitle().substring(0, 60),
          c: colour,
        });
      });
    });

  events.sort((x, y) => x.s - y.s || y.a - x.a);
  return json({now: Math.floor(now.getTime() / 1000), events: events.slice(0, MAX_EVENTS)});
}

function json(obj) {
  return ContentService.createTextOutput(JSON.stringify(obj))
    .setMimeType(ContentService.MimeType.JSON);
}
