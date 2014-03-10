var send_pebble = function(tz_sec, loc) {
  msg = {"tz_offset": tz_sec};
  if (loc) {
    msg.hemisphere = (loc.coords.latitude >= 0) ? 0 : 1;
  }
  console.log("msg.tz_offset=" + msg.tz_offset + " hemisphere=" + msg.hemisphere);
  Pebble.sendAppMessage(msg,
    function(e) {
      console.log("TimeZone info sent");
    },
    function(e) {
      console.log("TimeZone info send error: " + e.error.message);
    });  
}

var send_tz = function() {
  var d = new Date();
  var tz_sec = -60 * d.getTimezoneOffset();
  navigator.geolocation.getCurrentPosition(
    function(loc) {
      send_pebble(tz_sec, loc);
    },
    function(loc) {
      send_pebble(tz_sec, null);
    });
}

Pebble.addEventListener("ready",
  function(e) {
    send_tz();
    window.setInterval(send_tz, 60*1000);
  }
);
