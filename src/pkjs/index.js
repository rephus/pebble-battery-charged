// Battery threshold constant
var BATTERY_THRESHOLD = 80;

// Configuration page
Pebble.addEventListener('showConfiguration', function() {
  var pushbulletKey = localStorage.getItem('pushbullet_key') || '';
  var webhookUrl = localStorage.getItem('webhook_url') || '';
  var notifType = localStorage.getItem('notif_type') || 'pushbullet';
  var configUrl = 'data:text/html,' + encodeURIComponent(
    '<!DOCTYPE html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">' +
    '<title>Battery Charged Config</title>' +
    '<style>body{font-family:Arial,sans-serif;padding:20px;background:#f0f0f0}' +
    'input,select{width:100%;padding:10px;margin:10px 0;box-sizing:border-box;font-size:16px}' +
    'button{width:100%;padding:15px;background:#4CAF50;color:white;border:none;font-size:16px;cursor:pointer;margin-top:10px}' +
    'button:hover{background:#45a049}.info{background:#e7f3ff;padding:10px;margin:10px 0;border-radius:5px;font-size:14px}' +
    '.section{display:none;margin-top:15px}' +
    'label{font-weight:bold;display:block;margin-top:10px}</style>' +
    '</head><body>' +
    '<h2>Battery Charged Settings</h2>' +
    '<div class="info">Get notifications when your Pebble reaches '+BATTERY_THRESHOLD+'% battery.</div>' +

    '<label for="type">Notification Type:</label>' +
    '<select id="type" onchange="toggleSections()">' +
    '<option value="pushbullet"' + (notifType === 'pushbullet' ? ' selected' : '') + '>Pushbullet</option>' +
    '<option value="webhook"' + (notifType === 'webhook' ? ' selected' : '') + '>Custom Webhook</option>' +
    '<option value="none"' + (notifType === 'none' ? ' selected' : '') + '>None (Vibration Only)</option>' +
    '</select>' +

    '<div id="pushbullet-section" class="section">' +
    '<label for="pushbullet">Pushbullet Access Token:</label>' +
    '<input type="text" id="pushbullet" placeholder="o.aBcDeFgHiJkLmNoPqRsTuVwXyZ" value="' + pushbulletKey + '">' +
    '<div class="info">Get your token at: <a href="https://www.pushbullet.com/#settings/account" target="_blank">pushbullet.com/#settings/account</a></div>' +
    '</div>' +

    '<div id="webhook-section" class="section">' +
    '<label for="webhook">Webhook URL:</label>' +
    '<input type="url" id="webhook" placeholder="https://example.com/webhook" value="' + webhookUrl + '">' +
    '<div class="info">Examples:<br>- IFTTT: https://maker.ifttt.com/trigger/EVENT/with/key/KEY<br>' +
    '- Home Assistant: https://ha.com/api/webhook/ID</div>' +
    '</div>' +

    '<button onclick="saveConfig()">Save Settings</button>' +

    '<script>' +
    'function toggleSections(){' +
    'var type=document.getElementById("type").value;' +
    'document.getElementById("pushbullet-section").style.display=(type==="pushbullet")?"block":"none";' +
    'document.getElementById("webhook-section").style.display=(type==="webhook")?"block":"none";' +
    '}' +
    'toggleSections();' +
    'function saveConfig(){' +
    'var type=document.getElementById("type").value;' +
    'var pushbullet=document.getElementById("pushbullet").value;' +
    'var webhook=document.getElementById("webhook").value;' +
    'var result={notif_type:type,pushbullet_key:pushbullet,webhook_url:webhook};' +
    'document.location="pebblejs://close#"+encodeURIComponent(JSON.stringify(result));' +
    '}' +
    '</script></body></html>'
  );
  console.log('Opening configuration page');
  Pebble.openURL(configUrl);
});

// Save configuration
Pebble.addEventListener('webviewclosed', function(e) {
  if (e && e.response) {
    try {
      var config = JSON.parse(decodeURIComponent(e.response));
      console.log('Configuration saved: ' + JSON.stringify(config));

      if (config.notif_type) {
        localStorage.setItem('notif_type', config.notif_type);
        console.log('Notification type: ' + config.notif_type);
      }

      if (config.pushbullet_key) {
        localStorage.setItem('pushbullet_key', config.pushbullet_key);
        console.log('Pushbullet key saved');
      }

      if (config.webhook_url) {
        localStorage.setItem('webhook_url', config.webhook_url);
        console.log('Webhook URL saved: ' + config.webhook_url);
      }
    } catch (err) {
      console.log('Error parsing config: ' + err);
    }
  }
});

// Listen for messages from the watch
Pebble.addEventListener('appmessage', function(e) {
  console.log('Received message from watch: ' + JSON.stringify(e.payload));

  // Check if this is the battery alert message
  if (e.payload['100']) {  // MESSAGE_KEY_ALERT = 100
    console.log('Battery reached '+BATTERY_THRESHOLD+'% - alert triggered');

    var notifType = localStorage.getItem('notif_type') || 'none';

    if (notifType === 'pushbullet') {
      // Send Pushbullet notification
      var pushbulletKey = localStorage.getItem('pushbullet_key');

      if (pushbulletKey && pushbulletKey.length > 0) {
        var xhr = new XMLHttpRequest();
        xhr.open('POST', 'https://api.pushbullet.com/v2/pushes', true);
        xhr.setRequestHeader('Access-Token', pushbulletKey);
        xhr.setRequestHeader('Content-Type', 'application/json');

        xhr.onload = function() {
          if (xhr.status >= 200 && xhr.status < 300) {
            console.log('Pushbullet notification sent successfully: ' + xhr.status);
          } else {
            console.log('Pushbullet failed with status: ' + xhr.status + ' - ' + xhr.responseText);
          }
        };

        xhr.onerror = function() {
          console.log('Error sending Pushbullet notification');
        };

        var pushData = {
          type: 'note',
          title: 'Battery Charged!',
          body: 'Your Pebble reached '+BATTERY_THRESHOLD+'%. Unplug to preserve battery life.'
        };

        xhr.send(JSON.stringify(pushData));
        console.log('Sending Pushbullet notification...');
      }
      // Silently skip if no key configured

    } else if (notifType === 'webhook') {
      // Send custom webhook
      var webhookUrl = localStorage.getItem('webhook_url');

      if (webhookUrl && webhookUrl.length > 0) {
        var data = {
          title: 'Battery Charged!',
          message: 'Your Pebble reached '+BATTERY_THRESHOLD+'%. Unplug to preserve battery life.',
          battery_percent: BATTERY_THRESHOLD,
          timestamp: new Date().toISOString(),
          alert_type: 'pebble_battery_charged'
        };

        var xhr = new XMLHttpRequest();
        xhr.open('POST', webhookUrl, true);
        xhr.setRequestHeader('Content-Type', 'application/json');

        xhr.onload = function() {
          if (xhr.status >= 200 && xhr.status < 300) {
            console.log('Webhook notification sent successfully: ' + xhr.status);
          } else {
            console.log('Webhook failed with status: ' + xhr.status);
          }
        };

        xhr.onerror = function() {
          console.log('Error sending webhook notification');
        };

        xhr.send(JSON.stringify(data));
        console.log('Sending webhook to: ' + webhookUrl);
      }
      // Silently skip if no URL configured
    }
    // For 'none' type or unconfigured - just vibration, no logs needed

    console.log('Alert triggered at ' + new Date().toLocaleTimeString());
  }
});

Pebble.addEventListener('ready', function(e) {
  console.log('PebbleKit JS ready!');
  var notifType = localStorage.getItem('notif_type');

  if (notifType === 'pushbullet') {
    var pushbulletKey = localStorage.getItem('pushbullet_key');
    if (pushbulletKey && pushbulletKey.length > 0) {
      console.log('Pushbullet configured');
    }
  } else if (notifType === 'webhook') {
    var webhookUrl = localStorage.getItem('webhook_url');
    if (webhookUrl && webhookUrl.length > 0) {
      console.log('Webhook configured: ' + webhookUrl);
    }
  } else {
    console.log('Vibration only mode (notifications not configured)');
  }
});
