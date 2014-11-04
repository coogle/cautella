<?php
cautela_enable();

function __cautela_capture_output($file, $line, $value, $context) {

	var_dump(func_get_args());
	return false;
}

cautela_set_print_callback("__cautela_capture_output", 10);

print "Hello, world!";

?>
