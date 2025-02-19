/*
 * Copyright (C)2019 Roger Clark. VK3KYY / G4KYF
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <codeplug.h>
#include <HR-C6000.h>
#include <settings.h>
#include <ticks.h>
#include <user_interface/menuSystem.h>
#include <user_interface/uiUtilities.h>
#include <user_interface/uiLocalisation.h>
#include <functions/voicePrompts.h>

static char digits[9];
static int pcIdx;
static struct_codeplugContact_t contact;

static void updateCursor(void);
static void updateScreen(bool inputModeHasChanged);
static void handleEvent(uiEvent_t *ev);
static void announceContactName(void);

static const uint32_t CURSOR_UPDATE_TIMEOUT = 500;

static const char *menuName[4];
enum DISPLAY_MENU_LIST { ENTRY_TG = 0, ENTRY_PC, ENTRY_SELECT_CONTACT, ENTRY_USER_DMR_ID, NUM_ENTRY_ITEMS};

static menuStatus_t menuNumericalExitStatus = MENU_STATUS_SUCCESS;


// public interface
menuStatus_t menuNumericalEntry(uiEvent_t *ev, bool isFirstRun)
{
	if (isFirstRun)
	{
		menuName[0] = currentLanguage->tg_entry;
		menuName[1] = currentLanguage->pc_entry;
		menuName[2] = currentLanguage->contact;
		menuName[3] = currentLanguage->user_dmr_id;
		gMenusCurrentItemIndex = ENTRY_TG;
		digits[0] = 0;
		pcIdx = 0;
		updateScreen(true);
		return (MENU_STATUS_INPUT_TYPE | MENU_STATUS_SUCCESS);
	}
	else
	{
		// Clear input type beep for AudioAssist
		menuNumericalExitStatus = MENU_STATUS_SUCCESS;

		if (ev->events == EVENT_BUTTON_NONE)
		{
			updateCursor();
		}
		else
		{
			if (ev->hasEvent)
			{
				handleEvent(ev);
			}
			else
			{
				if ((gMenusCurrentItemIndex != ENTRY_SELECT_CONTACT) && (strlen(digits) <= NUM_PC_OR_TG_DIGITS))
				{
					updateCursor();
				}
			}
		}
	}
	return menuNumericalExitStatus;
}

static void updateCursor(void)
{
	size_t sLen;

	// Display blinking cursor only when digits could be entered.
	if ((gMenusCurrentItemIndex != ENTRY_SELECT_CONTACT) && ((sLen = strlen(digits)) <= NUM_PC_OR_TG_DIGITS))
	{
		static uint32_t lastBlink = 0;
		static bool     blink = false;
		uint32_t        m = fw_millis();

		if ((m - lastBlink) > CURSOR_UPDATE_TIMEOUT)
		{
			sLen *= 8;

			ucPrintCore((((DISPLAY_SIZE_X - sLen) >> 1) + sLen), (DISPLAY_SIZE_Y / 2), "_", FONT_SIZE_3, 0, blink);

			blink = !blink;
			lastBlink = m;

			ucRender();
		}
	}
}

static void updateScreen(bool inputModeHasChanged)
{
	char buf[33];
	size_t sLen = strlen(menuName[gMenusCurrentItemIndex]) * 8;
	int16_t y = 8;

	ucClearBuf();

	ucDrawRoundRectWithDropShadow(2, y - 1, (DISPLAY_SIZE_X - 6), ((DISPLAY_SIZE_Y / 8) - 1) * 3, 3, true);

	// Not really centered, off by 2 pixels
	ucPrintAt(((DISPLAY_SIZE_X - sLen) >> 1) - 2, y, (char *)menuName[gMenusCurrentItemIndex], FONT_SIZE_3);


	if (inputModeHasChanged)
	{
		if (nonVolatileSettings.audioPromptMode >= AUDIO_PROMPT_MODE_VOICE_LEVEL_1)
		{
			voicePromptsInit();
			switch(gMenusCurrentItemIndex)
			{
				case ENTRY_TG:
					voicePromptsAppendLanguageString(&currentLanguage->tg_entry);
					break;
				case ENTRY_PC:
					voicePromptsAppendLanguageString(&currentLanguage->pc_entry);
					break;
				case ENTRY_SELECT_CONTACT:
					voicePromptsAppendPrompt(PROMPT_CONTACT);
					{
						char buf[17];
						codeplugUtilConvertBufToString(contact.name, buf, 16);
						voicePromptsAppendString(buf);
					}
					break;
				case ENTRY_USER_DMR_ID:
					voicePromptsAppendString("ID");
					break;
			}
			voicePromptsPlay();
		}
	}

	if (pcIdx == 0)
	{
		ucPrintCentered((DISPLAY_SIZE_Y / 2), (char *)digits, FONT_SIZE_3);
	}
	else
	{
		codeplugUtilConvertBufToString(contact.name, buf, 16);
		ucPrintCentered((DISPLAY_SIZE_Y / 2), buf, FONT_SIZE_3);
		ucPrintCentered((DISPLAY_SIZE_Y - 12), (char *)digits, FONT_SIZE_1);
	}

	displayLightTrigger();
	ucRender();
}

static int getNextContact(int curidx, int dir, struct_codeplugContact_t *contact)
{
	int idx = curidx;

	do {
		idx += dir;
		if (idx >= 1024)
		{
			if (curidx == 0)
			{
				idx = 0;
				break;
			}
			idx = 1;
		}
		else
		{
			if (idx ==0)
			{
				if (curidx == 0)
				{
					idx = 0;
					break;
				}
				idx = 1024;
			}
		}
		codeplugContactGetDataForIndex(idx, contact);
	} while ((curidx != idx) && ((*contact).name[0] == 0xff));

	return idx;
}

static void announceContactName(void)
{
	if (nonVolatileSettings.audioPromptMode >= AUDIO_PROMPT_MODE_VOICE_LEVEL_1)
	{
		char buf[17];
		codeplugUtilConvertBufToString(contact.name, buf, 16);
		voicePromptsInit();
		voicePromptsAppendString(buf);
		voicePromptsPlay();
	}
}

static void handleEvent(uiEvent_t *ev)
{
	size_t sLen;
	uint32_t tmpID;

	displayLightTrigger();

	if ((nonVolatileSettings.audioPromptMode >= AUDIO_PROMPT_MODE_VOICE_LEVEL_1) && (ev->events & BUTTON_EVENT))
	{
		if (BUTTONCHECK_SHORTUP(ev, BUTTON_SK1))
		{
			if (!voicePromptIsActive)
			{
				voicePromptsInit();
				switch(gMenusCurrentItemIndex)
				{
					case ENTRY_TG:
						voicePromptsAppendPrompt(PROMPT_TALKGROUP);
						voicePromptsAppendString(digits);
						break;
					case ENTRY_PC:
						voicePromptsAppendLanguageString(&currentLanguage->private_call);
						voicePromptsAppendString(digits);
						break;
					case ENTRY_SELECT_CONTACT:
						voicePromptsAppendPrompt(PROMPT_CONTACT);
						{
							char buf[17];
							codeplugUtilConvertBufToString(contact.name, buf, 16);
							voicePromptsAppendString(buf);
						}
						break;
					case ENTRY_USER_DMR_ID:
						voicePromptsAppendString("ID");
						voicePromptsAppendString(digits);
						break;
				}
				voicePromptsPlay();
			}
			else
			{
				voicePromptsTerminate();
			}
		}
		return;
	}

	if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
	{
		menuSystemPopPreviousMenu();
		return;
	}
	else
	{
		if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
		{
			tmpID = atoi(digits);
			if ((tmpID > 0) && (tmpID <= MAX_TG_OR_PC_VALUE))
			{
				if (gMenusCurrentItemIndex != ENTRY_USER_DMR_ID)
				{
					if ((gMenusCurrentItemIndex == ENTRY_PC) || ((pcIdx != 0) && (contact.callType == 0x01)))
					{
						setOverrideTGorPC(tmpID, true);
					}
					else
					{
						setOverrideTGorPC(tmpID, false);
					}

					// Apply TS override, if any
					if (gMenusCurrentItemIndex == ENTRY_SELECT_CONTACT)
					{
						tsSetContactOverride(((menuSystemGetRootMenuNumber() == UI_CHANNEL_MODE) ? CHANNEL_CHANNEL : (CHANNEL_VFO_A + nonVolatileSettings.currentVFONumber)), &contact);
					}
				}
				else
				{
					trxDMRID = tmpID;
					if (BUTTONCHECK_DOWN(ev, BUTTON_SK2))
					{
						// make the change to DMR ID permanent if Function + Green is pressed
						codeplugSetUserDMRID(trxDMRID);
					}
				}
				announceItem(PROMPT_SEQUENCE_CONTACT_TG_OR_PC, PROMPT_THRESHOLD_3);
				inhibitInitialVoicePrompt = true;
				menuSystemPopAllAndDisplayRootMenu();
			}
			else
			{
				soundSetMelody(MELODY_ERROR_BEEP);
			}

		}
		else
		{
			if (KEYCHECK_SHORTUP(ev->keys, KEY_HASH))
			{
				pcIdx = 0;

				menuNumericalExitStatus |= MENU_STATUS_INPUT_TYPE;

				if ((BUTTONCHECK_DOWN(ev, BUTTON_SK2) != 0) && (gMenusCurrentItemIndex == ENTRY_SELECT_CONTACT))
				{
					snprintf(digits, 8, "%d", trxDMRID);
					digits[8] = 0;
					gMenusCurrentItemIndex = ENTRY_USER_DMR_ID;
				}
				else
				{
					gMenusCurrentItemIndex++;
					if (gMenusCurrentItemIndex > ENTRY_SELECT_CONTACT)
					{
						digits[0] = 0;
						gMenusCurrentItemIndex = ENTRY_TG;
					}
					else
					{
						if (gMenusCurrentItemIndex == ENTRY_SELECT_CONTACT)
						{
							menuNumericalExitStatus &= ~MENU_STATUS_INPUT_TYPE;

							pcIdx = getNextContact(0, 1, &contact);

							if (pcIdx != 0)
							{
								itoa(contact.tgNumber, digits, 10);
								menuNumericalExitStatus |= (MENU_STATUS_LIST_TYPE | MENU_STATUS_FORCE_FIRST);
							}
						}
					}
				}
				updateScreen(true);
			}
		}
	}

	if (gMenusCurrentItemIndex == ENTRY_SELECT_CONTACT)
	{
		int idx = pcIdx;

		if (KEYCHECK_PRESS(ev->keys, KEY_DOWN))
		{
			idx = getNextContact(pcIdx, 1, &contact);
			announceContactName();
		}
		else
		{
			if (KEYCHECK_PRESS(ev->keys, KEY_UP))
			{
				idx = getNextContact(pcIdx, -1, &contact);
				announceContactName();
			}
		}

		if (pcIdx != idx)
		{
			pcIdx = idx;
			itoa(contact.tgNumber, digits, 10);

			if (pcIdx == 1)
			{
				menuNumericalExitStatus |= (MENU_STATUS_LIST_TYPE | MENU_STATUS_FORCE_FIRST);
			}

			updateScreen(false);
		}
	}
	else
	{
		if ((sLen = strlen(digits)) <= NUM_PC_OR_TG_DIGITS)
		{
			bool refreshScreen = false;

			// Inc / Dec entered value.
			if (KEYCHECK_PRESS(ev->keys, KEY_UP) || KEYCHECK_PRESS(ev->keys, KEY_DOWN))
			{
				if (strlen(digits))
				{
					unsigned long int ccs7 = strtoul(digits, NULL, 10);

					if (KEYCHECK_PRESS(ev->keys, KEY_UP))
					{
						if (ccs7 < MAX_TG_OR_PC_VALUE)
						{
							ccs7++;
						}
						refreshScreen = true;
					}
					else
					{
						if (ccs7 > 1)
						{
							ccs7--;
						}
						refreshScreen = true;
					}

					if (refreshScreen)
					{
						sprintf(digits, "%lu", ccs7);
					}
				}
			} // Delete a digit
			else
			{
				if (KEYCHECK_PRESS(ev->keys, KEY_LEFT))
				{
					if ((sLen = strlen(digits)) > 0)
					{
						digits[sLen - 1] = 0;
						refreshScreen = true;
					}
				}
				else
				{
					// Add a digit
					if (sLen < NUM_PC_OR_TG_DIGITS)
					{
						int keyval = menuGetKeypadKeyValue(ev, true);

						if (keyval != 99)
						{
							char c[2] = {0, 0};
							c[0] = keyval + '0';

							if (nonVolatileSettings.audioPromptMode >= AUDIO_PROMPT_MODE_VOICE_LEVEL_1)
							{
								voicePromptsInit();
								voicePromptsAppendString(c);
								voicePromptsPlay();
							}

							strcat(digits, c);
							refreshScreen = true;
						}
					}
				}
			}

			if (refreshScreen)
			{
				updateScreen(false);
			}

			updateCursor();
		}
	}
}
