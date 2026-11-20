#pragma once

// Шаблон. Скопируй в include/location.h и подставь свои значения:
//     cp include/location.example.h include/location.h
// include/location.h исключён из git (см. .gitignore), поэтому город,
// координаты и часовой пояс не попадут в репозиторий.

#define CITY_NAME "Город"
#define CITY_LAT  "0.0000"
#define CITY_LON  "0.0000"

// Смещение от UTC в секундах. Примеры: Москва 3*3600, <город> 5*3600.
#define TZ_OFFSET_SEC (3 * 3600)