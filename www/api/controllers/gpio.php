<?
// GET /api/gpio/config

function GPIOConfig()
{
    global $fppDir;
    global $configDirectory;

    $basejson = file_get_contents('http://localhost/api/gpio');
    $all_gpios = json_decode($basejson, true);

    //add default additional status attributes
    for ($x = 0, $z = count($all_gpios); $x < $z; $x++) {
        $all_gpios[$x]['InUse'] = null;
        $all_gpios[$x]['Input'] = false;
        $all_gpios[$x]['Output'] = false;
        $all_gpios[$x]['CurrentValue'] = null;
        $all_gpios[$x]['Function'] = null;
        $all_gpios[$x]['Description'] = null;
        $all_gpios[$x]['ConfigError'] = null;
    }


    //check configs to pull in details of gpio assignments
    //gpio.json
    if (file_exists("$configDirectory/gpio.json")) {
        $gpio_config_json = file_get_contents("$configDirectory/gpio.json");
        $gpio_config = json_decode($gpio_config_json, true);
        $gpio_config_enabled = array_filter_by_value($gpio_config, 'enabled', true);

        foreach ($gpio_config_enabled as $x) {
            foreach ($all_gpios as $k => $gp) {
                // print_r($x['pin']);
                if ($gp['pin'] == $x['pin']) {
                    if ($gp['InUse'] == true) {
                        $all_gpios[$k]['ConfigError'] = true;
                    }
                    $all_gpios[$k]['Input'] = true;
                    $all_gpios[$k]['Function'] = 'GPIO Input';
                    $all_gpios[$k]['InUse'] = true;
                    $all_gpios[$k]['Description'] = $x['desc'];
                }
            }
        }
    }

    //co-other.json
    if (file_exists("$configDirectory/co-other.json")) {
        $co_other_config_json = file_get_contents("$configDirectory/co-other.json");
        $co_other_config = json_decode($co_other_config_json, true);
        $co_other_config_gpio = array_filter_by_value($co_other_config['channelOutputs'], 'type', 'GPIO');
        $co_other_config_gpio_enabled = array_filter_by_value($co_other_config_gpio, 'enabled', 1);

        foreach ($co_other_config_gpio_enabled as $x) {
            foreach ($all_gpios as $k => $gp) {
                if ($gp['gpio'] == $x['gpio']) {
                    if ($gp['InUse'] == true) {
                        $all_gpios[$k]['ConfigError'] = true;
                    }
                    $all_gpios[$k]['Output'] = true;
                    $all_gpios[$k]['Function'] = 'Channel Output';
                    $all_gpios[$k]['InUse'] = true;
                    $all_gpios[$k]['Description'] = $x['description'];
                }
            }
        }
    }
    //cape-inputs.json (cape hardwired gpios)
    if (file_exists("$configDirectory/cape-inputs.json")) {
        $cape_inputs_config_json = file_get_contents("$configDirectory/cape-inputs.json");
        $cape_inputs_config = json_decode($cape_inputs_config_json, true);
        $cape_inputs_config_gpio = array_filter_by_value($cape_inputs_config['inputs'], 'type', 'gpiod');

        foreach ($cape_inputs_config_gpio as $x) {
            foreach ($all_gpios as $k => $gp) {
                if ($gp['gpio'] == $x['gpio'] && $x['type'] == "gpiod") {
                    if ($gp['InUse'] == true) {
                        $all_gpios[$k]['ConfigError'] = true;
                    }
                    $all_gpios[$k]['Input'] = true;
                    $all_gpios[$k]['Function'] = 'Cape Controls';
                    $all_gpios[$k]['InUse'] = true;
                    $all_gpios[$k]['Description'] = "Interrupt for: " + $x['chip'];
                }
            }
        }
    }


    //Return Result
    return json($all_gpios);

}

// Function to filter a multi-dimensional array based on a specific value in a specified index
function array_filter_by_value($my_array, $index, $value)
{
    // Check if the input is a non-empty array
    if (is_array($my_array) && count($my_array) > 0) {
        // Loop through the keys of the input array
        foreach (array_keys($my_array) as $key) {
            // Extract the value at the specified index for the current key
            $temp[$key] = $my_array[$key][$index];

            // Check if the extracted value matches the desired value
            if ($temp[$key] == $value) {
                // If so, add the corresponding array to the new array
                $new_array[$key] = $my_array[$key];
            }
        }
    }
    // Return the filtered array
    return $new_array;
}

?>