idf_component_register(
	SRCS main.cpp
 		WebServer.cpp
        apds9960_sensor.cpp
  INCLUDE_DIRS "."
  REQUIRES
        driver
        esp_timer
        wifimanager
        nvsstoragemanager
        jsonwrapper
        esp_http_server
        button
)
