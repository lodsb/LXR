/*
 * buttonHandler.c
 *
 * Created: 24.04.2012 16:04:29
 *  Author: Julian
 */ 
#include "buttonHandler.h"
#include "Menu/menu.h"
#include "ledHandler.h"
#include "Hardware/lcd.h"
#include <stdio.h>
#include "frontPanelParser.h"
#include "IO\din.h"
#include "Menu\screensaver.h"
#include "Menu/copyClearTools.h"
#include "Hardware/timebase.h"
#include "config.h"
#include "front.h"


//volatile uint8_t buttonHandler_selectButtonMode; 


//used to remember the last active page when entering the sequencer mode
//needed to return to the previously shown page when seq mode button is released
uint8_t lastActivePage=0;
uint8_t lastActiveSubPage=0;
/** selected step for sequencer mode*/
uint8_t buttonHandler_selectedStep=0; //TODO ist das gleiche wie der parameter PAR_ACTIVE_STEP
uint8_t selectedStepLed=LED_STEP1;



static uint16_t buttonHandler_buttonTimer = 0;

#define TIMER_ACTION_OCCURED -2
static int8_t buttonHandler_buttonTimerStepNr = NO_STEP_SELECTED;
uint16_t buttonHandler_originalParameter = 0;	//saves parameter number for step automation reset (stgep assign)
uint8_t buttonHandler_originalValue = 0;		//saves the parameter value for reset
uint8_t buttonHandler_resetLock = 0;

volatile struct 
{
	unsigned selectButtonMode:3; // 0-3 for unshifted, 4-7 for shifted
	unsigned seqRunning:1;
	unsigned seqRecording:1;
	
	
}buttonHandler_stateMemory;

uint8_t buttonHandler_mutedVoices=0;
static int8_t buttonHandler_armedAutomationStep = NO_STEP_SELECTED;

#define ARM_AUTOMATION		0x40
#define DISARM_AUTOMATION	0x00

//--------------------------------------------------------
void buttonHandler_armTimerActionStep(int8_t stepNr)
{
	//check if sub or main step
	const uint8_t isMainStep = ( (stepNr%8)==0 );
	buttonHandler_armedAutomationStep = stepNr;
	
	if(isMainStep)
	{
		const uint8_t mainStepNr = (uint8_t)(stepNr / 8);
		led_setBlinkLed((uint8_t)(LED_STEP1 + mainStepNr),1);
	} else {
		const uint8_t selectButtonNr =(uint8_t)(stepNr%8);
		led_setBlinkLed((uint8_t)(LED_PART_SELECT1 + selectButtonNr),1);
	}
	
	
	frontPanel_sendData(ARM_AUTOMATION_STEP,(uint8_t)(stepNr),menu_getActiveVoice() | ARM_AUTOMATION);

	
	
}
//--------------------------------------------------------
void buttonHandler_disarmTimerActionStep()
{
	if(buttonHandler_armedAutomationStep != NO_STEP_SELECTED)
	{
		const uint8_t isMainStep = ( (buttonHandler_armedAutomationStep%8)==0 );
		if(isMainStep)
		{
			const uint8_t mainStepNr = (uint8_t)(buttonHandler_armedAutomationStep / 8);
			led_setBlinkLed((uint8_t)(LED_STEP1 + mainStepNr),0);
		} else {
			const uint8_t selectButtonNr = (uint8_t)(buttonHandler_armedAutomationStep%8);
			led_setBlinkLed((uint8_t)(LED_PART_SELECT1 + selectButtonNr),0);
		}
		
		//revert to original sound
		//make changes temporary while an automation step is armed - revert to original value
		if(buttonHandler_resetLock==1)
		{
			parameter_values[buttonHandler_originalParameter] = buttonHandler_originalValue;
		}		
		
		buttonHandler_armedAutomationStep = NO_STEP_SELECTED;
		frontPanel_sendData(ARM_AUTOMATION_STEP,0,DISARM_AUTOMATION);
		
		if(buttonHandler_resetLock==1)
		{
			buttonHandler_resetLock = 0;
			//&revert to original value
			if(buttonHandler_originalParameter<128) // => Sound Parameter
				{
					frontPanel_sendData(MIDI_CC,(uint8_t)(buttonHandler_originalParameter),buttonHandler_originalValue);
				}
				else if(buttonHandler_originalParameter>=128 && (buttonHandler_originalParameter < END_OF_SOUND_PARAMETERS)) // => Sound Parameter above 127
				{
					frontPanel_sendData(CC_2,(uint8_t)(buttonHandler_originalParameter-128),buttonHandler_originalValue);
				}
				else
				{
					menu_parseGlobalParam(buttonHandler_originalParameter,
							parameter_values[buttonHandler_originalParameter]);
				}
				menu_repaintAll();
		}		
		return;
	}
	
	buttonHandler_armedAutomationStep = NO_STEP_SELECTED;
	frontPanel_sendData(ARM_AUTOMATION_STEP,0,DISARM_AUTOMATION);

};
//--------------------------------------------------------
int8_t buttonHandler_getArmedAutomationStep()
{
	return buttonHandler_armedAutomationStep;
};
//--------------------------------------------------------
uint8_t buttonHandler_TimerActionOccured()
{
	buttonHandler_disarmTimerActionStep();
	if(buttonHandler_buttonTimerStepNr == TIMER_ACTION_OCCURED)//NO_STEP_SELECTED)
	{
		//a timed action apeared -> do nothing
		return 1;
	}
	buttonHandler_buttonTimerStepNr = NO_STEP_SELECTED;
	return 0;
}
void buttonHandler_setTimeraction(uint8_t buttonNr)
{
	buttonHandler_buttonTimer = time_sysTick+BUTTON_TIMEOUT;
	buttonHandler_buttonTimerStepNr = (int8_t)buttonNr;
	
}
//--------------------------------------------------------
/**returns 1 is the mode 2 select button is pressed*/
/*
uint8_t buttonHandler_isModeButtonPressed()
{
	const uint8_t arrayPos	= BUT_MODE/8;
	const uint8_t bitPos	= BUT_MODE&7;
	
	if(din_inputData[arrayPos] & (1<<bitPos))
	{
		return 0;
	}
	return 1;
}
*/

//--------------------------------------------------------
//periodically called handler for timed actions
//-> hold a step button for a long time to select/rec automation
void buttonHandler_tick()
{
	if( (time_sysTick > buttonHandler_buttonTimer))
	{
		if(buttonHandler_buttonTimerStepNr >=0)//!=NO_STEP_SELECTED)
		{
			//select step
			buttonHandler_armTimerActionStep(buttonHandler_buttonTimerStepNr);
			//reset
			buttonHandler_buttonTimerStepNr = TIMER_ACTION_OCCURED;//NO_STEP_SELECTED;
		}
	}
};
//--------------------------------------------------------

//--------------------------------------------------------
/**returns 1 is the shift button is pressed*/
uint8_t buttonHandler_getShift()
{
	const uint8_t arrayPos	= BUT_SHIFT/8;
	const uint8_t bitPos	= BUT_SHIFT&7;
	
	if(din_inputData[arrayPos] & (1<<bitPos))
	{
		return 0;
	}
	return 1;
}
//--------------------------------------------------------
void buttonHandler_handleModeButtons(uint8_t mode)
{
	if(buttonHandler_getShift())
	{
		//set the new mode
		buttonHandler_stateMemory.selectButtonMode = (uint8_t)((mode+4) & 0x07);
	}
	else
	{
		//set the new mode
		buttonHandler_stateMemory.selectButtonMode = (uint8_t)(mode & 0x07);
	}
	
	led_clearAllBlinkLeds();	
		

	//update the status LED
	led_setMode2(buttonHandler_stateMemory.selectButtonMode);
			
	switch(buttonHandler_stateMemory.selectButtonMode)
	{
		case SELECT_MODE_PERF:
		{
			led_clearSequencerLeds();
			led_clearSelectLeds();
			led_initPerformanceLeds();		

			//set menu to perf page
			lastActiveSubPage	= menu_getSubPage();
			menu_switchPage(PERFORMANCE_PAGE);
			menu_switchSubPage(0);
			menu_repaintAll();

				
		}
		break;
			
		case SELECT_MODE_STEP:
			//menu_switchPage(menu_getActiveVoice());
			led_setActiveSelectButton(menu_getSubPage()); 
			buttonHandler_enterSeqModeStepMode();
			break;
		case SELECT_MODE_VOICE:
		//set menu to voice page mode
			menu_switchPage(menu_getActiveVoice());
			led_setActiveSelectButton(menu_getSubPage()); 
			menu_resetActiveParameter();
		break;
		
		case SELECT_MODE_LOAD_SAVE:
				menu_switchPage(LOAD_PAGE);
		break;
			
		case SELECT_MODE_MENU:
		{
			uint8_t toggle = (menu_getActivePage() == MENU_MIDI_PAGE);
			menu_switchPage(MENU_MIDI_PAGE);
			
			if(toggle) {
				menu_switchSubPage(menu_getSubPage()); //to enable toggle
			}				
		}
		break;
			
		case SELECT_MODE_SOM_GEN:
#if USE_DRUM_MAP_GENERATOR
			menu_switchPage(SOM_PAGE);
#endif
		break;
			
		case SELECT_MODE_PAT_GEN:
			frontPanel_sendData(SEQ_CC,SEQ_REQUEST_EUKLID_PARAMS,menu_getActiveVoice());
			menu_switchPage(EUKLID_PAGE);
			
		break;
			
		default:
		break;
	}
}
//--------------------------------------------------------
void buttonHandler_muteVoice(uint8_t voice, uint8_t isMuted)
{
	DISABLE_CONV_WARNING
	if(isMuted)
	{
		buttonHandler_mutedVoices |= (1<<voice);

	} else
	{
		buttonHandler_mutedVoices &= ~(1<<voice);
		
	}
	END_DISABLE_CONV_WARNING

	//muted tracks are lit
	if(menu_muteModeActive)
	{
		led_setActiveVoiceLeds((uint8_t)(~buttonHandler_mutedVoices));
	}		
};
//--------------------------------------------------------
void buttonHandler_showMuteLEDs()
{
	led_setActiveVoiceLeds((uint8_t)(~buttonHandler_mutedVoices));
	menu_muteModeActive = 1;
}
//--------------------------------------------------------
void buttonHandler_handleSelectButton(uint8_t buttonNr)
{

	if(buttonHandler_getShift()) {
		
		switch(buttonHandler_stateMemory.selectButtonMode)
		{
			case SELECT_MODE_STEP:
			case SELECT_MODE_VOICE: {
				//select buttons represent sub steps
		
				uint8_t stepNr = (uint8_t)(buttonHandler_selectedStep + buttonNr);
				uint8_t ledNr = (uint8_t)(LED_PART_SELECT1 + buttonNr);
				//toggle the led
				led_toggle(ledNr);
				//TODO lastActivePage zeigt nur auf den currentTrack wenn man im stepMode ist... tut nicht in den anderen modes :-/
				//set sequencer step on soundchip
		
				uint8_t trackNr = menu_getActiveVoice(); //max 6 => 0x6 = 0b110
				uint8_t patternNr = menu_getViewedPattern(); //max 7 => 0x07 = 0b111
				uint8_t value = (uint8_t)((trackNr<<4) | (patternNr&0x7));
				frontPanel_sendData(STEP_CC,value,stepNr);
		
				//request step parameters from sequencer
				frontPanel_sendData(SEQ_CC,SEQ_REQUEST_STEP_PARAMS,stepNr);
		
				parameter_values[PAR_ACTIVE_STEP] = stepNr;
			}				
				break;
			case SELECT_MODE_PAT_GEN:
			case SELECT_MODE_PERF:
				//select shownPattern
					menu_setShownPattern(buttonNr);
					led_clearSelectLeds();
					led_clearAllBlinkLeds();
					led_setBlinkLed((uint8_t)(LED_PART_SELECT1+buttonNr),1);
					//led_clearSequencerLeds();
					//query current sequencer step states and light up the corresponding leds 
					uint8_t trackNr = menu_getActiveVoice(); //max 6 => 0x6 = 0b110
					uint8_t patternNr = menu_getViewedPattern(); //max 7 => 0x07 = 0b111
					uint8_t value = (uint8_t)((trackNr<<4) | (patternNr&0x7));
					frontPanel_sendData(LED_CC,LED_QUERY_SEQ_TRACK, value);
					frontPanel_sendData(SEQ_CC,SEQ_REQUEST_PATTERN_PARAMS, patternNr);
					frontPanel_sendData(SEQ_CC,SEQ_REQUEST_EUKLID_PARAMS, menu_activePage);
			break;
		}			
	} else {
		switch(buttonHandler_stateMemory.selectButtonMode)
		{
			
			case SELECT_MODE_STEP:
			{
				//select buttons represent sub steps
		
				uint8_t stepNr = (uint8_t)(buttonHandler_selectedStep + buttonNr);
				//uint8_t ledNr = LED_PART_SELECT1 + buttonNr;
			
				//request step parameters from sequencer
				frontPanel_sendData(SEQ_CC,SEQ_REQUEST_STEP_PARAMS,stepNr);
		
				parameter_values[PAR_ACTIVE_STEP] = stepNr;
				
				//buttonHandler_armTimerActionStep(stepNr);
				led_clearAllBlinkLeds();
				//re set the main step led
				led_setBlinkLed((uint8_t)(LED_STEP1 + (stepNr/8)),1);

				
				const uint8_t selectButtonNr = stepNr%8;
				led_setBlinkLed((uint8_t)(LED_PART_SELECT1 + selectButtonNr),1);
				
			}	
			break;
			
			
			case SELECT_MODE_VOICE:
			//change sub page -> osc, filter, mod etc...
			menu_switchSubPage(buttonNr);
			//go to 1st parameter on sub page
			menu_resetActiveParameter();
			led_setActiveSelectButton(buttonNr);
			menu_repaintAll();
			break;
		
			case SELECT_MODE_PERF:
			
			
				//check if euklid or perf mode is active
				if(menu_getActivePage() == PERFORMANCE_PAGE)
				{
					//todo
					//change pattern
					//if shift -> select pattern for edit
					
					if(buttonHandler_getShift())
					{
						
					} else {
						//pattern
						
						//tell sequencer to change pattern
						frontPanel_sendData(SEQ_CC,SEQ_CHANGE_PAT,buttonNr);
						//flash corresponding LED until ACK (SEQ_CHANGE_PAT) received
						uint8_t ledNr = (uint8_t)(LED_PART_SELECT1 + buttonNr);
						led_clearAllBlinkLeds();
						
						led_setBlinkLed(ledNr,1);
							
						//request the pattern info for the selected pattern (bar cnt, next...)
					//	frontPanel_sendData(SEQ_CC,SEQ_REQUEST_PATTERN_PARAMS,buttonNr);
						
						
					}
					
					

				}
				//----- Euklid Mode -----
				else
				{
					/* //moved to voice button
					//tell the sequencer the new active track
					//TODO muss nicht f�r button 8 gesendet werden
					frontPanel_sendData(SEQ_CC,SEQ_SET_ACTIVE_TRACK,buttonNr);
					//request the parameters for the new track
					frontPanel_sendData(SEQ_CC,SEQ_REQUEST_EUKLID_PARAMS,buttonNr);
					//the currently active track button is lit
					led_setActivePage(buttonNr);
					*/
					
										
				}					
			break;
		
	
		
			case SELECT_MODE_LOAD_SAVE:
				//the currently active button is lit
				led_setActivePage(buttonNr);
				
			//	menu_switchPage(LFO1_PAGE+buttonNr);
			break;
		}
	}		
	
}
//--------------------------------------------------------
void buttonHandler_updateSubSteps()
{
	led_clearSelectLeds();
	//query current sequencer step states and light up the corresponding leds 
	uint8_t trackNr = menu_getActiveVoice(); //max 6 => 0x6 = 0b110
	uint8_t patternNr = menu_getViewedPattern(); //max 7 => 0x07 = 0b111
	uint8_t value = (uint8_t)((trackNr<<4) | (patternNr&0x7));
	frontPanel_sendData(LED_CC,LED_QUERY_SEQ_TRACK,value);
}
//--------------------------------------------------------
/* 
-> change display to show selected step options (volume, probability, note etc...)
-> seq buttons select step
-> selected step is illuminated on the seq buttons
-> select buttons show 8 sub steps for the selected step
*/
void buttonHandler_enterSeqModeStepMode()
{
//	lastActivePage = menu_activePage;
//	lastActiveSubPage = menu_getSubPage();
	menu_switchSubPage(0);
	menu_switchPage(SEQ_PAGE);

	buttonHandler_updateSubSteps();
	
	led_setBlinkLed(selectedStepLed,1);
}
//--------------------------------------------------------
void buttonHandler_leaveSeqModeStepMode()
{
	//stop blinking active step led
	led_setBlinkLed(selectedStepLed,0);
	led_setValue(0,selectedStepLed);
	
	//reset select leds
	//led_clearSelectLeds();
	
//	menu_switchSubPage(lastActiveSubPage);
//	menu_switchPage(lastActivePage);
}
//--------------------------------------------------------
void buttonHandler_enterSeqMode()
{
	lastActivePage = menu_activePage;
	lastActiveSubPage = menu_getSubPage();
	menu_switchSubPage(0);
	menu_switchPage(SEQ_PAGE);

	buttonHandler_updateSubSteps();
	
	led_setBlinkLed(selectedStepLed,1);
}
//--------------------------------------------------------
void buttonHandler_leaveSeqMode()
{
	//stop blinking active step led
	led_setBlinkLed(selectedStepLed,0);
	led_setValue(0,selectedStepLed);
	
	//reset select leds
	led_clearSelectLeds();
	
	menu_switchPage(lastActivePage);
	menu_switchSubPage(lastActiveSubPage);
	
}
//--------------------------------------------------------
uint8_t buttonHandler_getMutedVoices()
{
	return buttonHandler_mutedVoices;
}
//--------------------------------------------------------
void buttonHandler_buttonReleased(uint8_t buttonNr)
{
	

	switch(buttonNr)
	{
		
		case BUT_COPY:
			if(buttonHandler_getShift())
			{
				
				
			}
			else
			{
				//copy mode abort/exit
				copyClear_reset();
				
			
			}

		
		break;
		
		case BUT_SHIFT: 
		/* when this button is released, revert back to normal operating mode
		*/
		
		//leave clear mode
		if(copyClear_Mode == MODE_CLEAR)
		{
			copyClear_armClearMenu(0);
			copyClear_Mode = MODE_NONE;
		}			
		
		led_setValue(0,LED_SHIFT);
		if(buttonHandler_getMode() == SELECT_MODE_VOICE) {
			buttonHandler_leaveSeqMode();			
		} else if( (buttonHandler_getMode() == SELECT_MODE_PERF)) {
			led_clearAllBlinkLeds();
			led_clearSelectLeds();
			
			menu_switchPage(PERFORMANCE_PAGE);
			led_initPerformanceLeds();
			
			return;
		} else if( (buttonHandler_getMode() == SELECT_MODE_PAT_GEN)) {
			//led_clearAllBlinkLeds();
			led_clearSelectLeds();
			led_setValue(1,(uint8_t)(menu_getViewedPattern() + LED_PART_SELECT1));
			menu_switchPage(EUKLID_PAGE);
		} else if ( (buttonHandler_getMode() == SELECT_MODE_STEP)) {
			buttonHandler_enterSeqModeStepMode();
		}			
		
		//show active voice if released
		if( (buttonHandler_getMode() != SELECT_MODE_PERF)) {
			led_setActiveVoice(menu_getActiveVoice());
		}else
		{
			buttonHandler_showMuteLEDs();
		}	
		
		break;
		
			//the sequencer buttons
		case BUT_SEQ1:
		case BUT_SEQ2:
		case BUT_SEQ3:
		case BUT_SEQ4:
		case BUT_SEQ5:
		case BUT_SEQ6:
		case BUT_SEQ7:
		case BUT_SEQ8:
		case BUT_SEQ9:
		case BUT_SEQ10:
		case BUT_SEQ11:
		case BUT_SEQ12:
		case BUT_SEQ13:
		case BUT_SEQ14:
		case BUT_SEQ15:
		case BUT_SEQ16:
		{
			
			
			
			//which button is released
			uint8_t seqButtonPressed;
			uint8_t ledNr;
			
			seqButtonPressed = buttonNr;
			ledNr = (uint8_t)(buttonNr+LED_STEP1);
			
			if(buttonHandler_getShift())
			{
				//do nothing if timer action occured
				if(buttonHandler_TimerActionOccured())return;
			
				if( (buttonHandler_getMode() == SELECT_MODE_VOICE) )
				{
					//sequencer mode -> buttons select active step
					//buttonHandler_selectActiveStep(ledNr, seqButtonPressed);
				} else if ( (buttonHandler_getMode() == SELECT_MODE_STEP) )
				{
					//buttonHandler_setRemoveStep(ledNr,seqButtonPressed);
				}
				
			}
			else
			{
			
				switch(buttonHandler_stateMemory.selectButtonMode)
				{
					case SELECT_MODE_STEP:
						//do nothing if timer action occured
						if(buttonHandler_TimerActionOccured())return;
						
						//buttonHandler_selectActiveStep(ledNr, seqButtonPressed);
					break;
					//--- edit the pattern -> button sets and removes a step ---
					case SELECT_MODE_VOICE:
					
						//do nothing if timer action occured
						if(buttonHandler_TimerActionOccured())return;
											
						buttonHandler_setRemoveStep(ledNr,seqButtonPressed);
					
					break;
					case SELECT_MODE_PERF:
						if(seqButtonPressed < 8 )
						{
							//turn roll off
							frontPanel_sendData(SEQ_CC,SEQ_ROLL_ON_OFF,(seqButtonPressed&0xf) );
							//turn button led off
							led_setValue(0,ledNr);
							
							
						}

					break;
	
					//--- unused (maybe lfo clock sync? ---
					case SELECT_MODE_LOAD_SAVE:
				
					break;
				
					default:
					break;
			
				}	
			}						
		}
		
		break;	
		
		//Part select buttons
		case BUT_SELECT1:
		case BUT_SELECT2:
		case BUT_SELECT3:
		case BUT_SELECT4:
		case BUT_SELECT5:
		case BUT_SELECT6:
		case BUT_SELECT7:
		case BUT_SELECT8:
			
			
			if(copyClear_Mode >= MODE_COPY_PATTERN)
			{
			
			}
			else
			{
				//if(buttonHandler_stateMemory.selectButtonMode == SELECT_MODE_VOICE) return;
				//do nothing if timer action occured
				if(buttonHandler_TimerActionOccured())return;
				
				buttonHandler_buttonTimerStepNr = NO_STEP_SELECTED;
				if(buttonHandler_getShift() && buttonHandler_stateMemory.selectButtonMode == SELECT_MODE_VOICE)
				{
					buttonHandler_handleSelectButton((uint8_t)(buttonNr-BUT_SELECT1));
				}					
			}			

		break;
	}		
}
//--------------------------------------------------------
void buttonHandler_selectActiveStep(uint8_t ledNr, uint8_t seqButtonPressed)
{
	
	led_setBlinkLed(selectedStepLed,0);
	led_setValue(0,selectedStepLed);
	
	if(buttonHandler_selectedStep == (seqButtonPressed*8))
	{
		menu_switchSubPage(menu_getSubPage()); //to enable toggle
	}		
				
	//update active step
	buttonHandler_selectedStep = (uint8_t)(seqButtonPressed*8);
	selectedStepLed = ledNr;
								
	parameter_values[PAR_ACTIVE_STEP] = buttonHandler_selectedStep;
				
	//blink new step
	led_setBlinkLed(ledNr,1);

	//update sub steps
	//request step parameters from sequencer 
	frontPanel_sendData(SEQ_CC,SEQ_REQUEST_STEP_PARAMS,(uint8_t)(seqButtonPressed*8));
				
	//update sub steps
	buttonHandler_updateSubSteps();
}
//--------------------------------------------------------
void buttonHandler_setRemoveStep(uint8_t ledNr, uint8_t seqButtonPressed)
{
	//led_toggle(ledNr); //handled by cortex
	led_setValue(0,ledNr);
	//we have 128 steps, the main buttons are only for multiples of 8
	DISABLE_CONV_WARNING
	seqButtonPressed *=8;
	END_DISABLE_CONV_WARNING
	//which track is active
	//uint8_t currentTrack = menu_getActivePage();
						
	//update active step (so that seq mode always shows the last set step)
	buttonHandler_selectedStep = seqButtonPressed;
	parameter_values[PAR_ACTIVE_STEP] = buttonHandler_selectedStep;
	selectedStepLed = ledNr;
				
	//request step parameters from sequencer
	frontPanel_sendData(SEQ_CC,SEQ_REQUEST_STEP_PARAMS,seqButtonPressed);
						
	//set sequencer step on soundchip
	uint8_t trackNr = menu_getActiveVoice(); //max 6 => 0x6 = 0b110
	uint8_t patternNr = menu_getViewedPattern(); //max 7 => 0x07 = 0b111
	uint8_t value = (uint8_t)((trackNr<<4) | (patternNr&0x7));
	//frontPanel_sendData(STEP_CC,value,seqButtonPressed);
	frontPanel_sendData(MAIN_STEP_CC,value,seqButtonPressed/8);		
}
//--------------------------------------------------------
void buttonHandler_buttonPressed(uint8_t buttonNr)
{

/*
char text[5];
lcd_setcursor(0,0);
itoa(buttonNr,text,10);
				
lcd_clear();
lcd_string(text);

led_toggle(buttonNr);
//led_setValue(0,buttonNr)
return;
*/
	screensaver_touch();

	switch(buttonNr)
	{
		//Part select buttons
		case BUT_SELECT1:
		case BUT_SELECT2:
		case BUT_SELECT3:
		case BUT_SELECT4:
		case BUT_SELECT5:
		case BUT_SELECT6:
		case BUT_SELECT7:
		case BUT_SELECT8:
			
			
			
			if(copyClear_Mode >= MODE_COPY_PATTERN)
			{
				//copy mode 
				if(copyClear_srcSet())
				{
					//select dest
					copyClear_setDst((int8_t)(buttonNr-BUT_SELECT1), MODE_COPY_PATTERN);
					copyClear_copyPattern();
					led_clearAllBlinkLeds();
					//query current sequencer step states and light up the corresponding leds 
					//query current sequencer step states and light up the corresponding leds 
					
					uint8_t trackNr = menu_getActiveVoice(); //max 6 => 0x6 = 0b110
					uint8_t patternNr = menu_getViewedPattern(); //max 7 => 0x07 = 0b111
					uint8_t value = (uint8_t)((trackNr<<4) | (patternNr&0x7));
					frontPanel_sendData(LED_CC,LED_QUERY_SEQ_TRACK,value);
				
				}
				else
				{
					//select src
					copyClear_setSrc((int8_t)(buttonNr-BUT_SELECT1),MODE_COPY_PATTERN);
					led_setBlinkLed(LED_PART_SELECT1+buttonNr-BUT_SELECT1,1);
				}
			}
			else
			{
				//moved to release button
				//buttonHandler_handleSelectButton(buttonNr-BUT_SELECT1);
				
				//select sub step when button is held
				//-> step mode
				//-> voice mode + shift
				
				if ( /*(buttonHandler_getMode() == SELECT_MODE_STEP) || */((buttonHandler_getMode() == SELECT_MODE_VOICE) && buttonHandler_getShift() ) )
				{
					//TODO hier muss selektiert werden!
					uint8_t selectButtonNr = (uint8_t)(buttonNr-BUT_SELECT1);
					uint8_t buttonNr = (uint8_t)(buttonHandler_selectedStep*8 + selectButtonNr);
					buttonHandler_setTimeraction(buttonNr);
				} else {
					
					buttonHandler_handleSelectButton((uint8_t)(buttonNr-BUT_SELECT1));
				}				

			}				
		break;
		
		case BUT_MODE1:
		case BUT_MODE2:
		case BUT_MODE3:
		case BUT_MODE4:
			buttonHandler_handleModeButtons((uint8_t)(buttonNr-BUT_MODE1));
		break;
	
		
		//Sequencer Start Stop button
		//because the output shift registers are full, this buttons LED is on a single uC pin
		case BUT_START_STOP:
			//toggle the led
			//send run/stop command to soundchip
			led_setValue(buttonHandler_stateMemory.seqRunning,LED_START_STOP);
			
			buttonHandler_stateMemory.seqRunning = (uint8_t)((1-buttonHandler_stateMemory.seqRunning)&0x01);
			frontPanel_sendData(SEQ_CC,SEQ_RUN_STOP,buttonHandler_stateMemory.seqRunning);

		break;
		
		case BUT_REC:
			if(buttonHandler_getShift()) {
				menu_switchPage(RECORDING_PAGE);
			} else {
				//toggle the led
				buttonHandler_stateMemory.seqRecording = (uint8_t)((1-buttonHandler_stateMemory.seqRecording) & 0x01);
				led_setValue(buttonHandler_stateMemory.seqRecording,LED_REC);
				//send run/stop command sequencer
				frontPanel_sendData(SEQ_CC,SEQ_REC_ON_OFF,buttonHandler_stateMemory.seqRecording);
			}				
			break;
			
		case BUT_VOICE_1:
		case BUT_VOICE_2:
		case BUT_VOICE_3:
		case BUT_VOICE_4:
		case BUT_VOICE_5:
		case BUT_VOICE_6:
		case BUT_VOICE_7:
		
		if(copyClear_Mode >= MODE_COPY_PATTERN)
		{
			//copy mode -> voice buttons select track copy src/dst
			if(copyClear_srcSet())
			{
				//select dest
				copyClear_setDst((int8_t)(buttonNr-BUT_VOICE_1),MODE_COPY_TRACK);
				copyClear_copyTrack();
				led_clearAllBlinkLeds();
				//query current sequencer step states and light up the corresponding leds 
				
				uint8_t trackNr = menu_getActiveVoice(); //max 6 => 0x6 = 0b110
				uint8_t patternNr = menu_getViewedPattern(); //max 7 => 0x07 = 0b111
				uint8_t value = (uint8_t)((trackNr<<4) | (patternNr&0x7));
				frontPanel_sendData(LED_CC,LED_QUERY_SEQ_TRACK,value);
				
			}
			else
			{
				//select src
				copyClear_setSrc((int8_t)(buttonNr-BUT_VOICE_1),MODE_COPY_TRACK);
				led_setBlinkLed((uint8_t)(LED_VOICE1+buttonNr-BUT_VOICE_1),1);
			}
		}
		else
		{
			
			uint8_t muteModeActive;
			muteModeActive = buttonHandler_getShift();
			if(buttonHandler_getMode() == SELECT_MODE_PERF)
			{
				//invert mute functionality
				muteModeActive = (uint8_t)(1-muteModeActive);
			}
			
		
			if(muteModeActive)
			{
				//un/mute
				const uint8_t voice = (uint8_t)(buttonNr-BUT_VOICE_1);
				
				if(buttonHandler_mutedVoices & (1<<voice))
				{
					//unmute tracks 0-7
					buttonHandler_muteVoice(voice,0);
					frontPanel_sendData(SEQ_CC,SEQ_UNMUTE_TRACK,voice);

				}
				else
				{
					//mute tracks 0-7
					buttonHandler_muteVoice(voice,1);
					frontPanel_sendData(SEQ_CC,SEQ_MUTE_TRACK,voice);

				}


			} 
			else
			{
				
				if(buttonHandler_getMode() == SELECT_MODE_PERF)
				{
					//unmute all tracks
						for(uint8_t i=0;i<=(buttonNr-BUT_VOICE_1);i++)
						{
							if(buttonHandler_mutedVoices & (1<<i))
							{
								frontPanel_sendData(SEQ_CC,SEQ_UNMUTE_TRACK,i);
								DISABLE_CONV_WARNING
								buttonHandler_mutedVoices &= ~(1<<i);
								END_DISABLE_CONV_WARNING
							}								
						}
						buttonHandler_showMuteLEDs();
						
				}
				else
				{
					//select active voice
				
					//the currently active button is lit
					led_setActiveVoice((uint8_t)(buttonNr-BUT_VOICE_1));
			
					//change voice page on display if in voice mode
					if( (buttonHandler_getMode() == SELECT_MODE_VOICE) /*|| (buttonHandler_getMode() ==SELECT_MODE_STEP) */)
					{
						menu_switchPage((uint8_t)(buttonNr-BUT_VOICE_1));
					}				
					frontPanel_sendData(SEQ_CC,SEQ_SET_ACTIVE_TRACK, (uint8_t)(buttonNr-BUT_VOICE_1));
			
					menu_setActiveVoice((uint8_t)(buttonNr-BUT_VOICE_1));
				
				
					frontPanel_sendData(SEQ_CC,SEQ_REQUEST_EUKLID_PARAMS,(uint8_t)(buttonNr-BUT_VOICE_1));
					//request the pattern info for the selected pattern (bar cnt, next...)
					//frontPanel_sendData(SEQ_CC,SEQ_REQUEST_PATTERN_PARAMS,buttonNr);
				
					if((buttonHandler_getMode() ==SELECT_MODE_STEP))
					{
						//reactivate sequencer mode
						led_clearAllBlinkLeds();
						buttonHandler_enterSeqModeStepMode();
					
					}
				}					
			}	
		}						
			
			
		break;
		
		//the sequencer buttons
		case BUT_SEQ1:
		case BUT_SEQ2:
		case BUT_SEQ3:
		case BUT_SEQ4:
		case BUT_SEQ5:
		case BUT_SEQ6:
		case BUT_SEQ7:
		case BUT_SEQ8:
		case BUT_SEQ9:
		case BUT_SEQ10:
		case BUT_SEQ11:
		case BUT_SEQ12:
		case BUT_SEQ13:
		case BUT_SEQ14:
		case BUT_SEQ15:
		case BUT_SEQ16:
		{
			
			
			
			//which button is pressed
			uint8_t seqButtonPressed;
			uint8_t ledNr;
			
			seqButtonPressed = buttonNr;
			ledNr = (uint8_t)(buttonNr+LED_STEP1);
			
			if(buttonHandler_getShift())
			{
				
				if( (buttonHandler_getMode() == SELECT_MODE_VOICE) )
				{
					//sequencer mode -> buttons select active step
					buttonHandler_selectActiveStep(ledNr, seqButtonPressed);
				} /*else if ( (buttonHandler_getMode() == SELECT_MODE_STEP) )
				{
					buttonHandler_setRemoveStep(ledNr,seqButtonPressed);
				}
				*/
				if ( (buttonHandler_getMode() == SELECT_MODE_STEP) )
				{
					
					//buttonHandler_setTimeraction(seqButtonPressed*8);
					buttonHandler_setRemoveStep(ledNr,seqButtonPressed);
					
				}					
				
			}
			else
			{
			
				switch(buttonHandler_stateMemory.selectButtonMode)
				{
					
					case SELECT_MODE_STEP:
						led_clearAllBlinkLeds();
						buttonHandler_selectActiveStep(ledNr, seqButtonPressed);
						
						//reset sub step to 1 (== main step parameters)
						led_setBlinkLed(LED_PART_SELECT1,1);
						
					break;
					//--- edit the pattern -> button sets and removes a step ---
					/*
					case SELECT_MODE_STEP:
						buttonHandler_setTimeraction(seqButtonPressed*8);
					break;
					*/
					case SELECT_MODE_VOICE:
					
						buttonHandler_setTimeraction((uint8_t)(seqButtonPressed*8));
						//buttonHandler_setRemoveStep(ledNr,seqButtonPressed);
					
					break;
					
					//--- buttons 1-8 initiate a manual roll
					case SELECT_MODE_PERF:
						if(seqButtonPressed < 8 )
						{
							//turn roll on
							frontPanel_sendData(SEQ_CC,SEQ_ROLL_ON_OFF,(uint8_t)((seqButtonPressed&0xf) + 0x10 ));
							//turn button led on
							led_setValue(1,ledNr);
							
						} 
						else
						{
							/*
							//moved pattern select to select buttons
							//pattern
						
							//tell sequencer to change pattern
							frontPanel_sendData(SEQ_CC,SEQ_CHANGE_PAT,seqButtonPressed-8);
							//flash corresponding LED until ACK (SEQ_CHANGE_PAT) received
							led_setBlinkLed(ledNr,1);
							
							//request the pattern info for the selected pattern (bar cnt, next...)
							frontPanel_sendData(SEQ_CC,SEQ_REQUEST_PATTERN_PARAMS,seqButtonPressed-8);
*/
						}
					break;
	
					//--- unused (maybe lfo clock sync? ---
					case SELECT_MODE_LOAD_SAVE:
				
					break;
				
					default:
					break;
			
				}	
			}						
			
			
			
			/*
			lcd_home();
			char text[16];
			sprintf(text,"cc:%d data:%d",currentTrack+SEQ_V1_TOGGLE_STEP,seqButtonPressed);
			lcd_string(text);
			*/
		}			
		break;

		case BUT_COPY:	
			if(buttonHandler_getShift())
			{
				//with shift -> clear mode
				if(copyClear_Mode == MODE_CLEAR)
				{
					//execute
					copyClear_executeClear();
					/*
					copyClear_clearCurrentTrack();
					copyClear_armClearMenu(0);
					copyClear_Mode = MODE_NONE;
					*/
				}
				else
				{
					copyClear_Mode = MODE_CLEAR;
					copyClear_armClearMenu(1);
				}					
				
			}
			else
			{
				//copy mode
				copyClear_Mode = MODE_COPY_TRACK;
				led_setBlinkLed(LED_COPY,1);
				led_clearSelectLeds();
				led_clearVoiceLeds();
			
			}
			return;
		
		break;
		
				
		//the mode selection for the 16 seq buttons
		case BUT_SHIFT: 
		/* while this button is pressed, the SEQUENCER mode is activated
		-> change display to show selected step options (volume, probability, note etc...)
		-> seq buttons select step
		-> selected step is illuminated on the seq buttons
		-> select buttons show 8 sub steps for the selected step
		
		->shows muted steps on voice leds while pressed
		*/
		
		led_setValue(1,LED_SHIFT);
		
		if(buttonHandler_getMode() == SELECT_MODE_VOICE)
		{
			buttonHandler_enterSeqMode();
		}
		else if( (buttonHandler_getMode() == SELECT_MODE_PERF) || (buttonHandler_getMode() == SELECT_MODE_PAT_GEN)) {
		
			//blink selected pattern LED
			menu_switchPage(PATTERN_SETTINGS_PAGE);
			led_clearSelectLeds();
			led_clearAllBlinkLeds();
			
			if((buttonHandler_getMode() == SELECT_MODE_PAT_GEN))
			{
				led_setBlinkLed(LED_MODE2,1);
			}
			
			//the pattern change update for the follow mode is not made immediately when the pattern options are active
			//so we have to do it here
			if (parameter_values[PAR_FOLLOW])  {
					menu_setShownPattern(menu_shownPattern);
					led_clearSequencerLeds();
					//query current sequencer step states and light up the corresponding leds 
					uint8_t trackNr = menu_getActiveVoice(); //max 6 => 0x6 = 0b110
					uint8_t patternNr = menu_getViewedPattern(); //max 7 => 0x07 = 0b111
					uint8_t value = (uint8_t)((trackNr<<4) | (patternNr&0x7));
					frontPanel_sendData(LED_CC,LED_QUERY_SEQ_TRACK,value);
					frontPanel_sendData(SEQ_CC,SEQ_REQUEST_PATTERN_PARAMS,frontParser_midiMsg.data2);
				}	
			
			
			led_setBlinkLed((uint8_t)(LED_PART_SELECT1 + menu_getViewedPattern()) ,1);
		}else if(buttonHandler_getMode() == SELECT_MODE_STEP)
		{
			buttonHandler_leaveSeqModeStepMode();
		}
		
		//show muted voices if pressed
		buttonHandler_showMuteLEDs();

		break;
		//the mode selection for the 8 select buttons

		
		default:
		break;
	}
	
	/*
	char text[17];
	lcd_clear();
	led_clearAll();
	sprintf(text,"but%d",buttonNr);
	lcd_string(text);
	led_setValue(1,buttonNr);
	*/
	
	
};
//--------------------------------------------------------------
void buttonHandler_toggleEuklidMode()
{
	if(menu_getActivePage() == PERFORMANCE_PAGE)
	{
		//switch to Euklid
		menu_switchPage(EUKLID_PAGE);
	}
	else
	{
		//switch back to mute mode
		menu_switchPage(PERFORMANCE_PAGE);	
	}
	
}
//--------------------------------------------------------------
uint8_t buttonHandler_getMode()
{
	return buttonHandler_stateMemory.selectButtonMode;
};
//--------------------------------------------------------------
