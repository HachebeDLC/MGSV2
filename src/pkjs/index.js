// PebbleKit JS entry point.
// - Clay renders the settings page and sends the values to the watch.
// - Weather comes from Open-Meteo (no API key required).

var Clay = require('@rebble/clay');
var clayConfig = require('./config');
var clay = new Clay(clayConfig);

function xhrRequest(url, type, callback) {
  var xhr = new XMLHttpRequest();
  xhr.onload = function () {
    callback(this.responseText);
  };
  xhr.open(type, url);
  xhr.send();
}

// Map an Open-Meteo WMO weather code to a short label.
function weatherCodeToCondition(code) {
  if (code === 0) return 'Clear';
  if (code <= 3) return 'Cloudy';
  if (code <= 48) return 'Fog';
  if (code <= 57) return 'Drizzle';
  if (code <= 67) return 'Rain';
  if (code <= 77) return 'Snow';
  if (code <= 82) return 'Showers';
  if (code <= 86) return 'Snow';
  if (code <= 99) return 'Storm';
  return '';
}

function sendWeather(tempC, conditions) {
  Pebble.sendAppMessage(
    { 'TEMPERATURE': tempC, 'CONDITIONS': conditions },
    function () { console.log('Weather sent: ' + tempC + 'C ' + conditions); },
    function (e) { console.log('Weather send failed: ' + JSON.stringify(e)); }
  );
}

function locationSuccess(pos) {
  var url = 'https://api.open-meteo.com/v1/forecast' +
    '?latitude=' + pos.coords.latitude +
    '&longitude=' + pos.coords.longitude +
    '&current=temperature_2m,weather_code';

  xhrRequest(url, 'GET', function (responseText) {
    try {
      var json = JSON.parse(responseText);
      var tempC = Math.round(json.current.temperature_2m);
      var conditions = weatherCodeToCondition(json.current.weather_code);
      sendWeather(tempC, conditions);
    } catch (err) {
      console.log('Weather parse error: ' + err);
    }
  });
}

function locationError(err) {
  console.log('Location error: ' + JSON.stringify(err));
}

function getWeather() {
  navigator.geolocation.getCurrentPosition(locationSuccess, locationError, {
    timeout: 15000,
    maximumAge: 60000
  });
}

Pebble.addEventListener('ready', function () {
  console.log('PebbleKit JS ready');
  getWeather();
});

Pebble.addEventListener('appmessage', function (e) {
  if (e.payload && e.payload.REQUEST_WEATHER) {
    getWeather();
  }
});
