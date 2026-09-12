<?
function returnIfExists($json, $setting) {
    if ($json == null) {
        return "";
    }
    if (array_key_exists($setting, $json)) {
        return $json[$setting];
    }
    return "";
}

function convertAndGetSettings() {
    global $settings;
        
    $cfgFile = $settings['configDirectory'] . "/plugin.lor-inputpup.json";
    if (file_exists($cfgFile)) {
        $j = file_get_contents($cfgFile);
        $json = json_decode($j, true);
        return $json;
    }
    $j = "{\"port\":\"\",\"speed\":19200,\"unitId\":\"0x01\",\"serialEvents\":[]}";
    return json_decode($j, true);
}

$pluginJson = convertAndGetSettings();
?>


<div id="global" class="settings">
<fieldset>
<legend>FPP LOR Input Pup Config</legend>

<script>

var serialEventConfig = <? echo json_encode($pluginJson, JSON_PRETTY_PRINT); ?>;


var uniqueId = 1;
var modelOptions = "";
function AddSerialEventItem(type, modType ) {
    var id = $("#serialeventTableBody > tr").length + 1;
    var html = "<tr class='fppTableRow";
    if (id % 2 != 0) {
        html += " oddRow'";
    }
    html += "'><td class='colNumber rowNumber'>" + id + ".</td><td><span style='display: none;' class='uniqueId'>" + uniqueId + "</span></td>";

    //html += DeviceSelect(SerialDevices, "");
    
    html += "<td><input type='text' minlength='7' maxlength='15' size='15' class='desc' /></td>";
    html += "<td><select class='conditiontype'>";
    html += "<option value='contains'";
    if(type == 'contains') {html += " selected ";}
    html += ">Contains</option><option value='startswith'";
    if(type == 'startswith') {html += " selected ";}
    html += ">Starts With</option><option value='endswith'";
    if(type == 'endswith') {html += " selected ";}
    html += ">Ends With</option><option value='regex'";
    if(type == 'regex') {html += " selected ";}
    html += ">Regex</option></select></td>";
    html += "<td><input type='text'  minlength='7' maxlength='15' size='15' class='conditionvalue' /></td>";
    
    html += "<td><select class='type'>";
    html += "<option value='none'";
    if(modType == 'none') {html += " selected ";}
    html += ">None</option><option value='substring'";
    if(modType == 'substring') {html += " selected ";}
    html += ">SubString</option><option value='regex'";
    if(modType == 'regex') {html += " selected ";}
    html += ">Regex</option></select></td>";

    html += "<td><input type='text'  minlength='7' maxlength='15' size='15' class='modifiervalue' /></td>";
    html += "<td><table class='fppTable' border=0 id='tableSerialCommand_" + uniqueId +"'></td>";
    html += "<td>Command:</td><td><select class='serialcommand' id='serialcommand" + uniqueId + "' onChange='CommandSelectChanged(\"serialcommand" + uniqueId + "\", \"tableSerialCommand_" + uniqueId + "\" , false, PrintArgsInputsForEditable);'><option value=''></option></select></td></tr>";
    html += "</table></td></tr>";
    //selected
    $("#serialeventTableBody").append(html);

    LoadCommandList($('#serialcommand' + uniqueId));

    newRow = $('#serialeventTableBody > tr').last();
    $('#serialeventTableBody > tr').removeClass('selectedEntry');
    DisableButtonClass('deleteEventButton');
    uniqueId++;

    return newRow;
}

function SaveSerialEventItem(row) {
    var id = $(row).find('.uniqueId').html();
    var desc = $(row).find('.desc').val();
	var conditiontype = $(row).find('.conditiontype').val();
    var conditionvalue = $(row).find('.conditionvalue').val();
    var modifiertype = $(row).find('.modifiertype').val();
    var modifiervalue = $(row).find('.modifiervalue').val();

    var json = {
        "description": desc,
        "condition": conditiontype,
        "conditionValue": conditionvalue,
        "modifier": modifiertype,
        "modifierValue": modifiervalue
    };

    CommandToJSON('serialcommand' + id, 'tableSerialCommand_' + id, json, true);
    return json;
}

function SaveSerialEventItems() {

    newserialeventConfig = { "port": '', "speed": 19200, "unitId": "0x01", "serialEvents": []};
    var i = 0;
    $("#serialeventTableBody > tr").each(function() {
        newserialeventConfig["serialEvents"][i++] = SaveSerialEventItem(this);
    });

    newserialeventConfig["port"] = document.getElementById("serialport").value;
    newserialeventConfig["speed"] = document.getElementById("serialspeed").value;
    newserialeventConfig["unitId"] = document.getElementById("serialunitid").value;

    var data = JSON.stringify(newserialeventConfig);
    $.ajax({
        type: "POST",
	url: 'api/configfile/plugin.lor-inputpup.json',
        dataType: 'json',
        async: false,
        data: data,
        processData: false,
        contentType: 'application/json',
        success: function (data) {
           SetRestartFlag(2);
        }
    });
}

function RefreshLastMessages() {
    $.get('api/plugin-apis/SERIALEVENT/list', function (data) {
          $("#lastMessages").text(data);
        }
    );
}

function RefreshInputStatus() {
    $.get('api/plugin-apis/SERIALEVENT/status', function (data) {
        var status;
        try {
            status = typeof data === "string" ? JSON.parse(data) : data;
        } catch (e) {
            return;
        }
        var inputs = status["inputs"] || [];
        for (var i = 0; i < 8; i++) {
            var el = $("#gpioStatus" + (i + 1));
            var label = (i + 1) + ": " + (inputs[i] ? "ON" : "OFF");
            if (inputs[i]) {
                el.addClass("gpioOn").removeClass("gpioOff").text(label);
            } else {
                el.addClass("gpioOff").removeClass("gpioOn").text(label);
            }
        }
    });
}


function RenumberRows() {
    var id = 1;
    $('#serialeventTableBody > tr').each(function() {
        $(this).find('.rowNumber').html('' + id++ + '.');
        $(this).removeClass('oddRow');

        if (id % 2 != 0) {
            $(this).addClass('oddRow');
        }
    });
}
function RemoveSerialEventItem() {
    if ($('#serialeventTableBody').find('.selectedEntry').length) {
        $('#serialeventTableBody').find('.selectedEntry').remove();
        RenumberRows();
    }
    DisableButtonClass('deleteEventButton');
}


$(document).ready(function() {
                  
    $('#serialeventTableBody').sortable({
        update: function(event, ui) {
            RenumberRows();
        },
        item: '> tr',
        scroll: true
    }).disableSelection();

    $('#serialeventTableBody').on('mousedown', 'tr', function(event,ui){
        $('#serialeventTableBody tr').removeClass('selectedEntry');
        $(this).addClass('selectedEntry');
        EnableButtonClass('deleteEventButton');
    });

    RefreshInputStatus();
    setInterval(RefreshInputStatus, 1000);
});

</script>

<style>
.gpioStatusBox { display: inline-block; width: 60px; margin: 2px; padding: 4px 0; text-align: center; border-radius: 4px; font-weight: bold; color: #fff; }
.gpioOff { background-color: #888; }
.gpioOn { background-color: #2e8b2e; }
</style>

<div>
Serial Port:<input type='text' id='serialport' minlength='7' maxlength='30' size='15' class='serialport' />
Speed:<input type='number' id='serialspeed' class='serialspeed' />
LOR Unit Id:<input type='text' id='serialunitid' minlength='1' maxlength='4' size='4' class='serialunitid' placeholder='0x01' />
<div>
Input Status:
<?
for ($i = 1; $i <= 8; $i++) {
    echo "<span id='gpioStatus$i' class='gpioStatusBox gpioOff'>$i: OFF</span>";
}
?>
</div>
<div>
<table border=0>
<tr><td colspan='2'>
        <input type="button" value="Save" class="buttons genericButton" onclick="SaveSerialEventItems();">
        <input type="button" value="Add" class="buttons genericButton" onclick="AddSerialEventItem('contains', 'none');">
        <input id="delButton" type="button" value="Delete" class="deleteEventButton disableButtons genericButton" onclick="RemoveSerialEventItem();">
    </td>
</tr>
</table>

<div class='fppTableWrapper fppTableWrapperAsTable'>
<div class='fppTableContents'>
<table class="fppSelectableRowTable" id="serialeventTable"  width='100%'>
<thead><tr class="fppTableHeader"><th>#</th><th></th><th>Description</th><th>Condition Type</th><th>Condition Value</th><th>Modifier Type</th><th>Modifier Value</th><th>Command</th></tr></thead>
<tbody id='serialeventTableBody'>
</tbody>
</table>
</div>

</div>
<div>
<p>
<div class="col-auto">
        <div>
            <div class="row">
                <div class="col">
                    Last Messages:&nbsp;<input type="button" value="Refresh" class="buttons" onclick="RefreshLastMessages();">
                </div>
            </div>
            <div class="row">
                <div class="col">
                    <pre id="lastMessages" style='min-width:150px; margin:1px;min-height:300px;'></pre>
                </div>
            </div>
        </div>
    </div>
<p>
</div>
</div>
<script>

$.each(serialEventConfig["serialEvents"], function( key, val ) {
    var row = AddSerialEventItem(val["condition"], val["modifier"]);
    $(row).find('.desc').val(val["description"]);
    $(row).find('.conditionvalue').val(val["conditionValue"]);
    $(row).find('.modifiervalue').val(val["modifierValue"]);

    var id = parseInt($(row).find('.uniqueId').html());
    PopulateExistingCommand(val, 'serialcommand' + id, 'tableSerialCommand_' + id, false, PrintArgsInputsForEditable);
});

document.getElementById("serialport").value = serialEventConfig["port"];
document.getElementById("serialspeed").value = serialEventConfig["speed"];
document.getElementById("serialunitid").value = serialEventConfig["unitId"];


</script>

</fieldset>
</div>
