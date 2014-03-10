var send_tz = function() {
  var d = new Date();
  var tz_sec = -60 * d.getTimezoneOffset();
  var hemisphere = 1;
  console.log("tz_sec=" + tz_sec + " hemisphere=" + hemisphere);
  Pebble.sendAppMessage({"tz_offset": tz_sec, "hemisphere": hemisphere},
    function(e) {
      console.log("TimeZone info sent");
    },
    function(e) {
      console.log("TimeZone info send error: " + e.error.message);
    });
}

Pebble.addEventListener("ready",
  function(e) {
    send_tz();
    window.setInterval(send_tz, 60*1000);
  }
);
