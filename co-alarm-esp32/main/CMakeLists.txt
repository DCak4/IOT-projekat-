idf_component_register(SRCS "simulation.c" "main.c" #.c fajlovi koji se kompajliraju
                    INCLUDE_DIRS "." #gde se traze .h
                    REQUIRES dht11 co_sensor led display alarm) #koje komponente
