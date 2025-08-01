<!DOCTYPE html>
<html lang="en">

<head>
    <?php
    include 'common/htmlMeta.inc';
    require_once 'config.php';
    include 'common/menuHead.inc';
    require_once "common.php";
    ?>
    <script type="text/javascript" src="js/validate.min.js"></script>

    <style>
        .pendingDhcpProxy {
            background-color: #fff3cd !important;
            color: darkred !important;
        }
    </style>

    <script language="Javascript">

        function UpdateLink(row) {
            var val = document.getElementById('ipRow' + row).value;
            var proxyLink = "";
            if (!(isValidHostname(val) || isValidIpAddress(val))) {
                proxyLink = "Must be either valid IP address or Valid Hostname"
            } else {
                <? if (!$settings['hideExternalURLs']) { ?>
                    proxyLink = "<a href='proxy/" + val + "'>" + val + "</a>";
                <? } ?>
            }
            document.getElementById('linkRow' + row).innerHTML = proxyLink;
        }

        function AddNewProxy() {
            var currentRows = $("#proxyTable > tbody > tr").length

            $('#proxyTable tbody').append(
                "<tr id='row'" + currentRows + " class='fppTableRow proxyRow'>" +
                "<td>" + (currentRows + 1) + "</td>" +
                "<td><input id='ipRow" + currentRows + "' class='ipaddress' type='text' size='40' oninput='UpdateLink(" + (currentRows) + ")'></td>" +
                "<td><input id='descRow" + currentRows + "' class='description' type='text' size='40' oninput='UpdateLink(" + (currentRows) + ")' value=''></td>" +
                "<td id='linkRow" + currentRows + "'> </td>" +
                "</tr>");
        }
        function AddProxyForHost(host, description = "") {
            var currentRows = $("#proxyTable > tbody > tr").length

            $('#proxyTable tbody').append(
                "<tr id='row'" + currentRows + " class='fppTableRow proxyRow'>" +
                "<td>" + (currentRows + 1) + "</td>" +
                "<td><input id='ipRow" + currentRows + "' class='ipaddress' type='text' size='40' oninput='UpdateLink(" + (currentRows) + ")' value='" + host + "'></td>" +
                "<td><input id='descRow" + currentRows + "' class='description' type='text' size='40' oninput='UpdateLink(" + (currentRows) + ")' value='" + description + "'></td>" +
                "<td id='linkRow" + currentRows + "'>" +
                <? if (!$settings['hideExternalURLs']) { ?>
                    "<a target='_blank' href='proxy/" + host + "'>" + host + "</a>" +
                <? } ?>
                "</td></tr>");
        }


        function RenumberColumns(tableName) {
            var id = 1;
            $('#' + tableName + ' tbody tr').each(function () {
                $this = $(this);
                $this.find("td:first").html(id);
                id++;
            });

        }
        function DeleteSelectedProxy() {
            if (tableInfo.selected >= 0) {
                $('#proxyTable tbody tr:nth-child(' + (tableInfo.selected + 1) + ')').remove();
                tableInfo.selected = -1;
                SetButtonState("#btnDelete", "disable");
                RenumberColumns("proxyTable");
            }
        }

        function SetProxies() {
            var formStr = "<form action='proxies.php' method='post' id='proxyForm'>";

            var json = new Array();
            $(".proxyRow").each(function () {
                var ip = $(this).find(".ipaddress").val();
                if (ip === undefined || ip === null || ip === '') { ip = ""; }
                if (isValidHostname(ip) || isValidIpAddress(ip)) {
                    var desc = $(this).find(".description").val();
                    if (desc === undefined || desc === null || desc === '') {
                        desc = "";
                    }
                    json.push({
                        "host": ip,
                        "description": desc
                    });
                }
            });
            if ($(".proxyRow").length == 0) {
                $.ajax({
                    url: 'api/proxies',
                    type: 'GET',
                    async: true,
                    dataType: 'json',
                    success: function (data) {
                        proxyInfos = data;
                        for (var i = 0; i < proxyInfos.length; i++) {
                            Delete("api/proxies/" + proxyInfos[i].host, true, null, true);
                        }
                    },
                    error: function () {
                        $.jGrowl('Error: Unable to get list of proxies', { themeState: 'danger' });
                    }
                });
            }

            Post("api/proxies", false, JSON.stringify(json, null, 2));
            location.reload();
        }

        var tableInfo = {
            tableName: "proxyTable",
            selected: -1,
            enableButtons: ["btnDelete"],
            disableButtons: []
        };

        $(document).ready(function () {
            SetupSelectableTableRow(tableInfo);

            $.ajax({
                url: 'api/proxies',
                type: 'GET',
                async: true,
                dataType: 'json',
                success: function (data) {
                    proxyInfos = data;
                    let hasPending = false;
                    for (var i = 0; i < proxyInfos.length; i++) {
                        if (proxyInfos[i].dhcp) {
                            let desc = proxyInfos[i].pending ? "DHCP (Not currently in saved proxy config)" : "DHCP";
                            let rowClass = proxyInfos[i].pending ? "pendingDhcpProxy" : "";
                            let descCellId = "dhcpDesc_" + proxyInfos[i].host.replace(/\./g, "_");
                            if (proxyInfos[i].pending) hasPending = true;
                            $("#proxyTable tbody").append(
                                "<tr class='" + rowClass + "' style='" + (proxyInfos[i].pending ? "background-color: #fff3cd;" : "") + "'>" +
                                "<td>" + (i + 1) + "</td>" +
                                "<td>&nbsp;&nbsp;&nbsp;" + proxyInfos[i].host + "</td>" +
                                "<td id='" + descCellId + "'>&nbsp;&nbsp;&nbsp;" + desc + "</td>" +
                                "<td><a target='_blank' href='proxy/" + proxyInfos[i].host + "/'>" + proxyInfos[i].host + "</a></td>" +
                                "</tr>"
                            );
                        } else {
                            AddProxyForHost(proxyInfos[i].host, proxyInfos[i].description);
                        }
                    }
                    if (hasPending) {
                        $(".backdrop").prepend(
                            "<div class='alert alert-danger' style='margin-bottom:10px;'><b>Note:</b> Highlighted DHCP hosts are detected on the network but are <u>not yet in the proxy configuration</u>. Click <b>Save</b> to add them.</div>"
                        );
                    }
                    for (var i = 0; i < proxyInfos.length; i++) {
                        if (proxyInfos[i].dhcp) {
                            (function (ip) {
                                let descCellId = "dhcpDesc_" + ip.replace(/\./g, "_");
                                $.ajax({
                                    url: "proxy/" + ip + "/api/system/info",
                                    type: "GET",
                                    dataType: "json",
                                    success: function (data) {
                                        if (data && data.HostName) {
                                            let cell = $("#" + descCellId);
                                            let currentDesc = cell.text();
                                            // Replace or append hostname
                                            if (currentDesc.indexOf("DHCP") !== -1) {
                                                cell.html(currentDesc.replace(/DHCP.*/, "DHCP (FPP Instance: " + data.HostName + ")" + (currentDesc.indexOf("Not in config") !== -1 ? " (Not in config)" : "")));
                                            } else {
                                                cell.html("DHCP (" + data.HostName + ")");
                                            }
                                        }
                                    }
                                });
                            })(proxyInfos[i].host);
                        }
                    }
                },
                error: function () {
                    $.jGrowl('Error: Unable to get list of proxies', { themeState: 'danger' });
                }
            });

        });
    </script>

    <title><? echo $pageTitle; ?></title>
</head>

<body>
    <div id="bodyWrapper">
        <?php
        $activeParentMenuItem = 'status';
        include 'menu.inc'; ?>
        <div class="mainContainer">
            <div class="title">Proxied Hosts</div>
            <div class="pageContent">

                <div id="proxies" class="settings">

                    <div class="row tablePageHeader">
                        <div class="col-md">
                            <h2>Proxied Hosts</h2>
                        </div>
                        <div class="col-md-auto ms-lg-auto">
                            <div class="form-actions">

                                <input type=button value='Delete' onClick='DeleteSelectedProxy();'
                                    data-btn-enabled-class="btn-outline-danger" id='btnDelete' class='disableButtons'>
                                <button type=button value='Add' onClick='AddNewProxy();'
                                    class='buttons btn-outline-success'><i class="fas fa-plus"></i> Add</button>
                                <input type=button value='Save' onClick='SetProxies();'
                                    class='buttons btn-success ml-1'>

                            </div>
                        </div>
                    </div>
                    <hr>

                    <div class="fppTableWrapper fppTableWrapperAsTable">
                        <div class='fppTableContents fppFThScrollContainer' role="region" aria-labelledby="proxyTable"
                            tabindex="0">
                            <table id="proxyTable" class="fppSelectableRowTable fppStickyTheadTable">
                                <thead>
                                    <tr>
                                        <th>#</td>
                                        <th>IP/HostName</td>
                                        <th>Description</td>
                                        <th>
                                            <? if (!$settings['hideExternalURLs']) { ?>
                                                Link
                                            <? } ?>
                                        </th>
                                    </tr>
                                </thead>
                                <tbody>
                                </tbody>
                            </table>
                        </div>
                    </div>

                    <div class="backdrop">
                        <b>Notes:</b>
                        <p>This is a list of ip/hostnames for which we can reach their HTTP configuration pages via
                            proxy through this FPP instance.
                        <p>
                        <p>For example, if this FPP instance is used as a wireless proxy to another e1.31 controller
                            where FPP communicates to the "show network" via the WIFI adapter and to the controller via
                            the ethernet adapter, you can put the IP address of the e1.31 controller here and access the
                            configuration pages by proxy without having to setup complex routing tables.
                        </p>
                    </div>
                </div>

            </div>
        </div>
        <?php include 'common/footer.inc'; ?>
    </div>



</body>

</html>