#include <gui/containers/mainmenu.hpp>

#include <texts/TextKeysAndLanguages.hpp>

#include <touchgfx/Unicode.hpp>



mainmenu::mainmenu()

{



}



void mainmenu::initialize()

{

    mainmenuBase::initialize();

}

#ifndef SIMULATOR

void mainmenu::updateText(int16_t value)

{

	switch(value) {

		case 0: {

			Unicode::snprintf(textAreaMainMenuBuffer, TEXTAREAMAINMENU_SIZE, "%s", touchgfx::TypedText(TEXTS(T_MODE)).getText());

		}break;

		case 1: {

			Unicode::snprintf(textAreaMainMenuBuffer, TEXTAREAMAINMENU_SIZE, "%s", touchgfx::TypedText(TEXTS(T_SOUND)).getText());

		}break;

		case 2: {

			Unicode::snprintf(textAreaMainMenuBuffer, TEXTAREAMAINMENU_SIZE, "%s", touchgfx::TypedText(TEXTS(T_CONNECT)).getText());

		}break;

		case 3: {

			Unicode::snprintf(textAreaMainMenuBuffer, TEXTAREAMAINMENU_SIZE, "%s", touchgfx::TypedText(TEXTS(T_JURNAL)).getText());

		}break;

		case 4: {

			Unicode::snprintf(textAreaMainMenuBuffer, TEXTAREAMAINMENU_SIZE, "%s", touchgfx::TypedText(TEXTS(T_DEVICES)).getText());

		}break;

		case 5: {

			Unicode::fromUTF8(reinterpret_cast<const uint8_t*>("РЕЖИМ ЗОН"), textAreaMainMenuBuffer, TEXTAREAMAINMENU_SIZE);

		}break;

		case 6: {

			Unicode::snprintf(textAreaMainMenuBuffer, TEXTAREAMAINMENU_SIZE, "%s", touchgfx::TypedText(TEXTS(T_TEST)).getText());

		}break;

		default: {

			textAreaMainMenuBuffer[0] = 0;

		}break;

	}



	textAreaMainMenu.invalidate();

}



void mainmenu::updateConnectionText(int16_t value)

{

	switch (value) {

	case 0:

		Unicode::fromUTF8(reinterpret_cast<const uint8_t*>("WIFI"), textAreaMainMenuBuffer, TEXTAREAMAINMENU_SIZE);

		break;

	case 1:

		Unicode::fromUTF8(reinterpret_cast<const uint8_t*>("RS-485"), textAreaMainMenuBuffer, TEXTAREAMAINMENU_SIZE);

		break;

	default:

		textAreaMainMenuBuffer[0] = 0;

		break;

	}

	textAreaMainMenuBuffer[TEXTAREAMAINMENU_SIZE - 1] = 0;

	textAreaMainMenu.invalidate();

}

void mainmenu::updateTestSelectText(int16_t value)
{
	switch (value) {
	case 0:
		Unicode::fromUTF8(reinterpret_cast<const uint8_t*>("ТЕСТ1"), textAreaMainMenuBuffer, TEXTAREAMAINMENU_SIZE);
		break;
	case 1:
		Unicode::fromUTF8(reinterpret_cast<const uint8_t*>("ТЕСТ2"), textAreaMainMenuBuffer, TEXTAREAMAINMENU_SIZE);
		break;
	case 2:
		Unicode::fromUTF8(reinterpret_cast<const uint8_t*>("ТЕСТ3"), textAreaMainMenuBuffer, TEXTAREAMAINMENU_SIZE);
		break;
	default:
		textAreaMainMenuBuffer[0] = 0;
		break;
	}
	textAreaMainMenuBuffer[TEXTAREAMAINMENU_SIZE - 1] = 0;
	textAreaMainMenu.invalidate();
}

void mainmenu::updateTestLampText(int16_t value)
{
	static const char* const kNames[] = {
		"ПИТ",
		"НОРМ",
		"ПУСК",
		"СТОП",
		"НЕИСП",
		"ПОЖАР",
		"АВТО",
		"П.ОБЩ К",
		"ОСТ К",
		"П.СП К",
		"П.ОБЩ Т",
		"ОСТ Т",
		"П.СП Т",
		"ВВОД",
		"ОТМ"
	};
	const int16_t n = (int16_t)(sizeof(kNames) / sizeof(kNames[0]));
	if (value < 0 || value >= n) {
		textAreaMainMenuBuffer[0] = 0;
	} else {
		Unicode::fromUTF8(reinterpret_cast<const uint8_t*>(kNames[value]),
		                  textAreaMainMenuBuffer, TEXTAREAMAINMENU_SIZE);
	}
	textAreaMainMenuBuffer[TEXTAREAMAINMENU_SIZE - 1] = 0;
	textAreaMainMenu.invalidate();
}

void mainmenu::updateTestSoundText(int16_t value)
{
	static const char* const kNames[] = {
		"ПОЖАР1 С",
		"ПОЖАР1 Д",
		"ПОЖАР2 С",
		"ПОЖАР2 Д",
		"НЕИСПР С",
		"НЕИСПР Д",
		"ПУСК"
	};
	const int16_t n = (int16_t)(sizeof(kNames) / sizeof(kNames[0]));
	if (value < 0 || value >= n) {
		textAreaMainMenuBuffer[0] = 0;
	} else {
		Unicode::fromUTF8(reinterpret_cast<const uint8_t*>(kNames[value]),
		                  textAreaMainMenuBuffer, TEXTAREAMAINMENU_SIZE);
	}
	textAreaMainMenuBuffer[TEXTAREAMAINMENU_SIZE - 1] = 0;
	textAreaMainMenu.invalidate();
}

#endif

