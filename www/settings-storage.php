<?
$skipJSsettings = 1;
require_once('common.php');
?>

<script type="text/javascript" src="jquery/jQuery.msgBox/scripts/jquery.msgBox.js"></script>
<link href="jquery/jQuery.msgBox/styles/msgBoxLight.css" rel="stylesheet" type="text/css">

<script>
function StorageDialogDone() {
    EnableModalDialogCloseButton("storageSettingsProgress");
    $('#storageSettingsProgressCloseButton').prop("disabled", false);
    SetSetting("LastBlock", "0", 0, 1);
}
function growSDCardFS() {
    DisplayConfirmationDialog("growSDCard", "Grow Filesystem", $("#dialog-confirm"), function() {
        DisplayProgressDialog("storageSettingsProgress", "Storage Expand");
        StreamURL('growsd.php?wrapped=1', 'storageSettingsProgressText', 'StorageDialogDone');
    });
}
function newSDCardPartition() {
    DisplayConfirmationDialog("growSDCard", "Grow Filesystem", $("#dialog-confirm-newpartition"), function() {
        DisplayProgressDialog("storageSettingsProgress", "Storage Expand");
        StreamURL('newpartitionsd.php?wrapped=1', 'storageSettingsProgressText', 'StorageDialogDone');
    });
}
    
function checkForStorageCopy() {
    $.msgBox({
             title: "Copy settings?",
             content: "Would you like to copy all files to the new storage location?\nAll settings on the new storage will be overwritten.",
             type: "info",
             buttons: [{ value: "Yes" }, { value: "No" }],
             success: function (result) {
                 if (result == "Yes") {
                    DisplayProgressDialog("storageSettingsProgress", "Storage Expand");
                    StreamURL("copystorage.php?wrapped=1&storageLocation=" + $('#storageDevice').val() + "&direction=TOUSB&delete=no&path=/&flags=All", 'storageSettingsProgressText', 'StorageDialogDone');
                 }
            }
        });
}

function checkFormatStorage()
{
    var value = $('#storageDevice').val();
    
    var e = document.getElementById("storageDevice");
    var name = e.options[e.selectedIndex].text;
    if (name.includes("Not Mounted")) {
        var btitle = "Format Storage Location (" + value + ")" + name;
        $.msgBox({ type: "prompt",
                 title: btitle,
                 inputs: [
                     { header: "Don't Format", type: "radio", name: "formatType", checked:"", value: "none" },
                     { header: "ext4 (Most stable)", type: "radio", name: "formatType", value: "ext4" },
                     { header: "FAT (Compatible with Windows/OSX, unsupported, slow, not recommended)", type: "radio", name: "formatType", value: "FAT"}
                 ],
                 buttons: [ { value: "OK" } ],
                 opacity: 0.5,
                 success: function (result, values) {
                 var v = $('input[name=formatType]:checked').val();
                 if (v != "none") {
                    $.ajax({ url: "formatstorage.php?fs=" + v + "&storageLocation=" + $('#storageDevice').val(),
                        async: false,
                        success: function(data) {
                           checkForStorageCopy();
                        },
                        failure: function(data) {
                        DialogError("Formate Storage", "Error formatting storage.");
                        }
                        });
                    } else {
                        checkForStorageCopy();
                    }
                 }
                 });
    }
}
    
<?php
if ($settings['Platform'] == "BeagleBone Black") {
?>
function flashEMMC() {
    DisplayConfirmationDialog("flashEMMC", "Flash to eMMC", $("#dialog-confirm-emmc"), function() {
        window.location.href="flashbbbemmc.php";
    });
}
function flashEMMCBtrfs() {
    DisplayConfirmationDialog("flashEMMC", "Flash to eMMC", $("#dialog-confirm-emmc"), function() {
        window.location.href="flashbbbemmc-btrfs.php";
    });
}
<?php
}
?>
function flashUSBDone() {
    $('#flashUSBProgressCloseButton').prop("disabled", false);
    EnableModalDialogCloseButton("flashUSBProgress");
}
function flashUSB() {
    DisplayConfirmationDialog("flashUSB", "Flash to USB", $("#dialog-confirm-usb"), function() {
        DisplayProgressDialog("flashUSBProgress", "Flash to USB");
        StreamURL("flash-pi-usb.php", 'flashUSBProgressText', 'flashUSBDone', 'flashUSBDone');
    });
}
</script>

<?php
    
function PrintStorageDeviceSelect($platform)
{
	global $SUDO;

	# FIXME, this would be much simpler by parsing "lsblk -l"
	exec('lsblk -l | grep /boot | cut -f1 -d" " | sed -e "s/p[0-9]$//"', $output, $return_val);
    if (count($output) > 0) {
        $bootDevice = $output[0];
    } else {
        $bootDevice = "";
    }
	unset($output);

    if ($platform == "BeagleBone Black") {
        exec('findmnt -n -o SOURCE / | colrm 1 5', $output, $return_val);
        $rootDevice = $output[0];
        unset($output);
        
        if ($bootDevice == "") {
            exec('findmnt -n -o SOURCE / | colrm 1 5 | sed -e "s/p[0-9]$//"', $output, $return_val);
            $bootDevice = $output[0];
            unset($output);
        }
    } else {
        exec('lsblk -l | grep " /$" | cut -f1 -d" "', $output, $return_val);
        $rootDevice = $output[0];
        unset($output);
    }

	$storageDevice = "";
	exec('grep "fpp/media" /etc/fstab | cut -f1 -d" " | sed -e "s/\/dev\///"', $output, $return_val);
	if (isset($output[0]))
		$storageDevice = $output[0];
	unset($output);

	$found = 0;
	$values = Array();

	foreach(scandir("/dev/") as $fileName)
	{
		if ((preg_match("/^sd[a-z][0-9]/", $fileName)) ||
			(preg_match("/^mmcblk[0-9]p[0-9]/", $fileName)))
		{
			exec($SUDO . " sfdisk -s /dev/$fileName", $output, $return_val);
			$GB = intval($output[0]) / 1024.0 / 1024.0;
			unset($output);

			if ($GB <= 0.1)
				continue;

			$FreeGB = "Not Mounted";
			exec("df -k /dev/$fileName | grep $fileName | awk '{print $4}'", $output, $return_val);
			if (count($output))
			{
				$FreeGB = sprintf("%.1fGB Free", intval($output[0]) / 1024.0 / 1024.0);
				unset($output);
			}
			else
			{
				unset($output);

				if (preg_match("/^$rootDevice/", $fileName))
				{
					exec("df -k / | grep ' /$' | awk '{print \$4}'", $output, $return_val);
					if (count($output))
						$FreeGB = sprintf("%.1fGB Free", intval($output[0]) / 1024.0 / 1024.0);
					unset($output);
				}
			}

			$key = $fileName . " ";
			$type = "";

			if (preg_match("/^$bootDevice/", $fileName))
			{
				$type .= " (boot device)";
			}

			if (preg_match("/^sd/", $fileName))
			{
				$type .= " (USB)";
			}

			$key = sprintf( "%s - %.1fGB (%s) %s", $fileName, $GB, $FreeGB, $type);

			$values[$key] = $fileName;

			if ($storageDevice == $fileName)
				$found = 1;
		}
	}

	if (!$found)
	{
		$arr = array_reverse($values, true);
		$values = array_reverse($arr);
	}
    if ($storageDevice == "") {
        $storageDevice = $rootDevice;
    }

	PrintSettingSelect('StorageDevice', 'storageDevice', 0, 1, $storageDevice, $values, "", "checkFormatStorage");
}
    

$addnewfsbutton = false;
$addflashbutton = false;
exec('findmnt -n -o SOURCE / | colrm 1 5', $output, $return_val);
$rootDevice = $output[0];
if ($rootDevice == 'mmcblk0p1' || $rootDevice == 'mmcblk0p2') {
    if (isset($settings["LastBlock"]) && $settings['LastBlock'] < 8000000 && $settings['LastBlock'] > 0) {
        $addnewfsbutton = true;
    }
    if ($settings['Platform'] == "BeagleBone Black") {
        if (strpos($settings['SubPlatform'], 'PocketBeagle') === FALSE) {
            $addflashbutton = true;
        }
    }
    if ((strpos($settings['SubPlatform'], "Raspberry Pi 4") !== false) && (file_exists("/dev/sda"))) {
        $addflashbutton = true;
    }
}
if ($addnewfsbutton) {
?>
<br>
<h3>SD Card Actions:</h3>
<div class="row">
    <div class="col-md-2"><input style='width:13em;' type='button' class='buttons' value='Grow Filesystem' onClick='growSDCardFS();'></div>
    <div class="col-md-10">This will grow the file system on the SD card to use the entire size of the SD card.</div>
</div>

<? if ($uiLevel >= 1) { ?>
    <div class="row mt-2">
        <div class="col-md-2"><input style='width:13em;' type='button' class='buttons' value='New Partition' onClick='newSDCardPartition();'></div>
        <div class="col-md-10"><b>*</b>&nbsp;This will create a new partition in the unused aread of the SD card.  The new partition can be selected as a storage location and formatted to BTRFS or ext4 after a reboot.
</div>
    </div>
<? } ?>

    <hr class="mt-2 mb-2">
<?php
}
if ($addflashbutton) {
    if ($settings['Platform'] == "BeagleBone Black") {
?>
<h3>eMMC Actions:</h3>

    <div class="row">
        <div class="col-auto"><input style='width:13em;' type='button' class='buttons' value='Flash to eMMC' onClick='flashEMMC();'></div>
        <div class="col-auto">&nbsp;This will copy FPP to the internal eMMC.</div>
    </div>
<? if ($uiLevel >= 1) { ?>
    <div class="row mt-2">
        <div class="col-auto"><input style='width:13em;' type='button' class='buttons' value='Flash to eMMC' onClick='flashEMMCBtrfs();'></div>
        <div class="col-auto"><i class='fas fa-fw fa-graduation-cap ui-level-1'></i>&nbsp;This will copy FPP to the internal eMMC, but use BTRFS for the root filesystem.<br>BTRFS uses compression to save a lot of space on the eMMC, but at the expense of extra CPU usage.</div>
    </div>
<?
   }
} else if ($settings['Platform'] == "Raspberry Pi") {
?>
    <h3>USB Actions:</h3>
    <div class="row">
        <div class="col-auto"><input style='width:13em;' type='button' class='buttons' value='Flash to USB' onClick='flashUSB();'></div>
        <div class="col-auto">&nbsp;This will copy FPP to the USB device.  See note below for more information.</div>
    </div>    
<?
}
}

if ($settings['Platform'] != "Docker" ) { ?>
<br><br>
    <b>Storage Device:</b> &nbsp;<? PrintStorageDeviceSelect($settings['Platform']); ?>

<? if (strpos($settings['SubPlatform'], "Raspberry Pi 4") === false) { ?>

    <div class="callout callout-warning">
    Changing the storage device to anything other than the SD card is strongly discouraged.   There are all kinds of problems that using USB storage introduce into the system which can easily result in various problems include network lag, packet drops, audio clicks/pops, high CPU usage, etc...  Using USB storage also results in longer bootup time.   In addition, many advanced features and various capes/hats are known to NOT work when using USB storage.
<br><br>
In addition to the above, since it is not recommended, using USB storage is not tested nearly as extensively by the FPP developers.   Thus, upgrades (even "patch" upgrades) have a higher risk of unexpected problems.   By selecting a USB storage device, you assume much higher risk of problems and issues than when selecting an SD partition.   
    </div>
<? } else { ?>
    <div class="callout callout-warning">
        If using a USB storage device, it is STRONGLY recommended that the device be a USB 3.0 SATA/SSD device or other fast storage and not a generic USB Thumb drive.   Older USB devices, even on the USB 3.0 ports, are known to cause all kinds of problems including network lag, packet drops, audio clicks/pops, high CPU usage, etc...
        <br><br>
        In addition, a good cooling solution, particularly for the USB HUB chips on the Pi, is critical.  It is recommended to have a cooling fan and heat syncs on the Pi4 chips to keep everything cool.  When the chips get too hot, the entire system is throttled which introduces latency and lag.
    </div>
    <br>
<?
}
}
?>

<div id="dialog-confirm" class="hidden">
<p><span class="ui-icon ui-icon-alert" style="flat:left; margin: 0 7px 20px 0;"></span>Growing the filesystem will require a reboot to take effect.  Do you wish to proceed?</p>
</div>
<div id="dialog-confirm-emmc" class="hidden">
<p><span class="ui-icon ui-icon-alert" style="flat:left; margin: 0 7px 20px 0;"></span>Flashing the eMMC can take a long time.  Do you wish to proceed?</p>
</div>
<div id="dialog-confirm-usb" class="hidden">
<p><span class="ui-icon ui-icon-alert" style="flat:left; margin: 0 7px 20px 0;"></span>Flashing the USB can take a long time and will destroy all content on the USB device.  Do you wish to proceed?</p>
</div>
<div id="dialog-confirm-newpartition" class="hidden">
<p><span class="ui-icon ui-icon-alert" style="flat:left; margin: 0 7px 20px 0;"></span>Creating a new partition in the unused space will require a reboot to take effect.  Do you wish to proceed?</p>
</div>

