// Clay configuration page for the MGSV watchface.
// Values are sent to the watch keyed by `messageKey` (see package.json).
module.exports = [
  {
    "type": "heading",
    "defaultValue": "MGSV Watchface"
  },
  {
    "type": "text",
    "defaultValue": "Ajustes del reloj / Watchface settings"
  },
  {
    "type": "section",
    "items": [
      {
        "type": "heading",
        "defaultValue": "Tiempo / Time"
      },
      {
        "type": "select",
        "messageKey": "TimeFormat",
        "label": "Formato de hora / Time format",
        "defaultValue": "0",
        "options": [
          { "label": "Automatico (sistema) / Auto", "value": "0" },
          { "label": "12 horas / 12h", "value": "1" },
          { "label": "24 horas / 24h", "value": "2" }
        ]
      }
    ]
  },
  {
    "type": "section",
    "items": [
      {
        "type": "heading",
        "defaultValue": "Clima / Weather"
      },
      {
        "type": "toggle",
        "messageKey": "ShowWeather",
        "label": "Mostrar clima / Show weather",
        "defaultValue": true
      },
      {
        "type": "toggle",
        "messageKey": "TemperatureUnit",
        "label": "Usar Fahrenheit / Use Fahrenheit",
        "defaultValue": false
      }
    ]
  },
  {
    "type": "section",
    "items": [
      {
        "type": "heading",
        "defaultValue": "Energia / Power"
      },
      {
        "type": "toggle",
        "messageKey": "PowerSaving",
        "label": "Ahorro de energia - oculta los segundos / Power saving",
        "defaultValue": false
      }
    ]
  },
  {
    "type": "submit",
    "defaultValue": "Guardar / Save"
  }
];
