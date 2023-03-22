<?

/////////////////////////////////////////////////////////////////////////////
// GET /oled
function GetOLEDStatus()
{
    global $settings;

    return "OLED Status";
}

/////////////////////////////////////////////////////////////////////////////
// GET /oled/action/options
function GetOLEDMenuActions()
{
    global $settings;
    $menu_options = Array();
    $menu_options = ["Up","Down","Left","Right","Enter","Test"];

    return json($menu_options);
}

/////////////////////////////////////////////////////////////////////////////
// GET /oled/action/up
function SetOLEDActionUp()
{
    global $settings;

    return "OLED Up";
}

/////////////////////////////////////////////////////////////////////////////
// GET /oled/action/down

function SetOLEDActionDown()
{
    global $settings;

    return "OLED Up";
}

/////////////////////////////////////////////////////////////////////////////
// GET /oled/action/left

function SetOLEDActionLeft()
{
    global $settings;

    return "OLED Left";
}

/////////////////////////////////////////////////////////////////////////////
// GET /oled/action/right

function SetOLEDActionRight()
{
    global $settings;

    return "OLED Right";
}

/////////////////////////////////////////////////////////////////////////////
// GET /oled/action/enter

function SetOLEDActionEnter()
{
    global $settings;

    return "OLED Enter";
}

/////////////////////////////////////////////////////////////////////////////
// GET /oled/action/test

function SetOLEDActionTest()
{
    global $settings;

    return "OLED Test";
}

?>
