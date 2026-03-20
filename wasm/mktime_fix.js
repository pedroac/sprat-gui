mergeInto(LibraryManager.library, {
  _mktime_js__i53abi: true,
  _mktime_js__deps: ['$ydayFromDate'],
  _mktime_js: function(tmPtr) {
    // Standard tm struct offsets (all 4-byte ints):
    // tm_sec: 0, tm_min: 4, tm_hour: 8, tm_mday: 12, tm_mon: 16, tm_year: 20
    // tm_wday: 24, tm_yday: 28, tm_isdst: 32
    
    var year = HEAP32[(tmPtr + 20) >> 2] + 1900;
    var mon = HEAP32[(tmPtr + 16) >> 2];
    var mday = HEAP32[(tmPtr + 12) >> 2];
    var hour = HEAP32[(tmPtr + 8) >> 2];
    var min = HEAP32[(tmPtr + 4) >> 2];
    var sec = HEAP32[(tmPtr + 0) >> 2];
    var dst = HEAP32[(tmPtr + 32) >> 2];

    if (!isFinite(year)) year = 1970;
    if (!isFinite(mon)) mon = 0;
    if (!isFinite(mday)) mday = 1;
    if (!isFinite(hour)) hour = 0;
    if (!isFinite(min)) min = 0;
    if (!isFinite(sec)) sec = 0;
    if (!isFinite(dst)) dst = -1;

    // Clamp to reasonable ranges to avoid invalid Date values.
    if (year < 1900) year = 1900;
    if (year > 9999) year = 9999;
    if (mon < 0) mon = 0;
    if (mon > 11) mon = 11;
    if (mday < 1) mday = 1;
    if (mday > 31) mday = 31;
    if (hour < 0) hour = 0;
    if (hour > 23) hour = 23;
    if (min < 0) min = 0;
    if (min > 59) min = 59;
    if (sec < 0) sec = 0;
    if (sec > 60) sec = 60;

    var date = new Date(year, mon, mday, hour, min, sec, 0);
    if (isNaN(date.getTime())) {
      // Fall back to epoch instead of aborting.
      date = new Date(0);
    }

    // DST handling (mirrors upstream logic, with clamped inputs)
    var guessedOffset = date.getTimezoneOffset();
    var start = new Date(date.getFullYear(), 0, 1);
    var summerOffset = new Date(date.getFullYear(), 6, 1).getTimezoneOffset();
    var winterOffset = start.getTimezoneOffset();
    var dstOffset = Math.min(winterOffset, summerOffset);

    if (dst < 0) {
      HEAP32[(tmPtr + 32) >> 2] = Number(summerOffset != winterOffset && dstOffset == guessedOffset);
    } else if ((dst > 0) != (dstOffset == guessedOffset)) {
      var nonDstOffset = Math.max(winterOffset, summerOffset);
      var trueOffset = dst > 0 ? dstOffset : nonDstOffset;
      date.setTime(date.getTime() + (trueOffset - guessedOffset) * 60000);
    }

    HEAP32[(tmPtr + 24) >> 2] = date.getDay();
    var yday = ydayFromDate(date) | 0;
    HEAP32[(tmPtr + 28) >> 2] = yday;
    HEAP32[(tmPtr + 0) >> 2] = date.getSeconds();
    HEAP32[(tmPtr + 4) >> 2] = date.getMinutes();
    HEAP32[(tmPtr + 8) >> 2] = date.getHours();
    HEAP32[(tmPtr + 12) >> 2] = date.getDate();
    HEAP32[(tmPtr + 16) >> 2] = date.getMonth();
    HEAP32[(tmPtr + 20) >> 2] = date.getYear();

    var timeMs = date.getTime();
    if (isNaN(timeMs)) {
      return -1;
    }
    return timeMs / 1000;
  },

  jsHaveAsyncify: function() {
    return (typeof Asyncify !== 'undefined' || (typeof Module !== 'undefined' && !!Module['Asyncify']));
  },

  jsHaveJspi: function() {
    return (typeof WebAssembly !== 'undefined' && typeof WebAssembly.Suspender !== 'undefined');
  }
});
