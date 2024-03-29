#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <functiondiscoverykeys_devpkey.h>
#include "PolicyConfig.h"

struct DeviceInfo {
	LPWSTR Id;
	LPWSTR Name;

	// Volume
	float VolumeScalar;
	float VolumeLevel;
	BOOL IsMute;

	// Device flags
	EDataFlow DataFlow;
	BOOL IsDefaultPlayback;
	BOOL IsDefaultCommunicationPlayback;
	BOOL IsDefaultRecording;
	BOOL IsDefaultCommunicationRecording;
	DWORD State;
};

struct Device{
	DeviceInfo Info;

	IMMDevice* Device;
	IPropertyStore* PropertyStore;
	IAudioEndpointVolume* AudioEndpointVolume;
	IMMEndpoint* Endpoint;
};

struct DefaultDevices {
	LPWSTR Playback;
	LPWSTR CommunicationPlayback;

	LPWSTR Recording;
	LPWSTR CommunicationRecording;
};

static UINT NumDevices;
static Device* AllDevices;

IMMDeviceEnumerator* DeviceEnumerator;
IPolicyConfig* PolicyConfig;

static void PopulateInfo(Device* device, DefaultDevices* defaultDevices)
{
	device->Device->GetId(&device->Info.Id);
	device->Device->GetState(&device->Info.State);

	PROPVARIANT varProperty;
	device->PropertyStore->GetValue(PKEY_Device_FriendlyName, &varProperty);
	device->Info.Name = varProperty.pwszVal;

	device->Endpoint->GetDataFlow(&device->Info.DataFlow);

	if (device->Info.State == DEVICE_STATE_ACTIVE)
	{
		device->AudioEndpointVolume->GetMasterVolumeLevelScalar(&device->Info.VolumeScalar);
		device->AudioEndpointVolume->GetMasterVolumeLevel(&device->Info.VolumeLevel);
		device->AudioEndpointVolume->GetMute(&device->Info.IsMute);

		if (lstrcmpW(device->Info.Id, defaultDevices->Playback) == 0)
			device->Info.IsDefaultPlayback = TRUE;
		if (lstrcmpW(device->Info.Id, defaultDevices->CommunicationPlayback) == 0)
			device->Info.IsDefaultCommunicationPlayback = TRUE;

		if (lstrcmpW(device->Info.Id, defaultDevices->Recording) == 0)
			device->Info.IsDefaultRecording = TRUE;
		if (lstrcmpW(device->Info.Id, defaultDevices->CommunicationRecording) == 0)
			device->Info.IsDefaultCommunicationRecording = TRUE;
	}
}

static void GetDefaultDevices(DefaultDevices* defaultDevices)
{
	IMMDevice* device;

	if (SUCCEEDED(DeviceEnumerator->GetDefaultAudioEndpoint(EDataFlow::eRender, ERole::eMultimedia, &device))) {
		device->GetId(&defaultDevices->Playback);
	}
	else {
		printf("No Default Playback Device\n");
	}

	if (SUCCEEDED(DeviceEnumerator->GetDefaultAudioEndpoint(EDataFlow::eRender, ERole::eCommunications, &device))) {
		device->GetId(&defaultDevices->CommunicationPlayback);
	}
	else {
		printf("No Default Playback Communication Device\n");
	}

	if (SUCCEEDED(DeviceEnumerator->GetDefaultAudioEndpoint(EDataFlow::eCapture, ERole::eMultimedia, &device))) {
		device->GetId(&defaultDevices->Recording);
	}
	else {
		printf("No Default Recording Device\n");
	}

	if (SUCCEEDED(DeviceEnumerator->GetDefaultAudioEndpoint(EDataFlow::eCapture, ERole::eCommunications, &device))) {
		device->GetId(&defaultDevices->CommunicationRecording);
	}
	else {
		printf("No Default Recording Communication Device\n");
	}

	printf("\n");
}

static void PopulateAllDevices(void)
{
	DefaultDevices defaultDevices;
	GetDefaultDevices(&defaultDevices);

	for (int i = 0; i < NumDevices; i++)
	{
		PopulateInfo(&AllDevices[i], &defaultDevices);
	}
}

static void InitializeAndPopulateAllDevices(void)
{
	int MaxDevices = 256;
	NumDevices = 0;

	if (AllDevices == NULL)
		AllDevices = (Device*)VirtualAlloc(0, sizeof(Device) * MaxDevices, MEM_COMMIT, PAGE_READWRITE);

	if (DeviceEnumerator == NULL)
		CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&DeviceEnumerator));

	if (PolicyConfig == NULL)
		CoCreateInstance(__uuidof(CPolicyConfigClient), NULL, CLSCTX_ALL, __uuidof(IPolicyConfig), (LPVOID*)&PolicyConfig);

	IMMDeviceCollection* deviceCollectionPtr = NULL;
	DeviceEnumerator->EnumAudioEndpoints(eAll, DEVICE_STATE_ACTIVE | DEVICE_STATE_DISABLED | DEVICE_STATE_UNPLUGGED, &deviceCollectionPtr);

	UINT count;
	deviceCollectionPtr->GetCount(&count);

	if (count > MaxDevices)
	{
		printf("Too many devices. Max is %i\n", MaxDevices);
		return;
	}

	Device* currDevice = AllDevices;

	NumDevices = count;

	for (int i = 0; i < count; i++)
	{
		deviceCollectionPtr->Item(i, &currDevice->Device);
		currDevice->Device->OpenPropertyStore(STGM_READ, &currDevice->PropertyStore);
		currDevice->Device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, NULL, (void**)&currDevice->AudioEndpointVolume);
		currDevice->Device->QueryInterface(__uuidof(IMMEndpoint), (void**)&currDevice->Endpoint);

		currDevice++;
	}

	PopulateAllDevices();
}

bool match(const wchar_t* pattern, const wchar_t* candidate, int p, int c) {
	if (pattern[p] == L'\0')
	{
		return candidate[c] == L'\0';
	}
	else if (pattern[p] == L'*')
	{
		for (; candidate[c] != L'\0'; c++)
		{
			if (match(pattern, candidate, p + 1, c))
				return true;
		}

		return match(pattern, candidate, p + 1, c);
	}
	else if (pattern[p] != candidate[c])
	{
		return false;
	}
	else
	{
		return match(pattern, candidate, p + 1, c + 1);
	}
}

static void SetDevicesWhere(float volumeScalar, BOOL mute, const wchar_t* pattern, bool invert)
{
	for (int i = 0; i < NumDevices; i++)
	{
		Device* device = &AllDevices[i];

		bool isMatch = match(pattern, device->Info.Name, 0, 0);
		if (invert)
			isMatch = !isMatch;

		if (!isMatch)
			continue;

		PolicyConfig->SetEndpointVisibility(device->Info.Id, true);
		device->Device->GetState(&device->Info.State);

		if (device->Info.State == DEVICE_STATE_ACTIVE)
		{
			device->AudioEndpointVolume->SetMasterVolumeLevel(volumeScalar, &GUID_NULL);
			device->AudioEndpointVolume->SetMasterVolumeLevelScalar(volumeScalar, &GUID_NULL);
			device->AudioEndpointVolume->SetMute(mute, &GUID_NULL);
		}
	}
}

static bool SetDefaultDevicesWhere(ERole role, EDataFlow dataFlow, const wchar_t* pattern)
{
	bool flag = false;
	for (int i = 0; i < NumDevices; i++)
	{
		Device* device = &AllDevices[i];

		if (!match(pattern, device->Info.Name, 0, 0) || device->Info.DataFlow != dataFlow)
			continue;

		PolicyConfig->SetDefaultEndpoint(device->Info.Id, role);
		flag = true;
		break;
	}

	return flag;
}

static void EnableAllDevices()
{
	for (int i = 0; i < NumDevices; i++)
	{
		Device* device = &AllDevices[i];
		PolicyConfig->SetEndpointVisibility(device->Info.Id, true);
	}
}

static void DisableAllDevices()
{
	for (int i = 0; i < NumDevices; i++)
	{
		Device* device = &AllDevices[i];
		PolicyConfig->SetEndpointVisibility(device->Info.Id, false);
	}
}

static void RandomizeAllDevices()
{
	srand(time(NULL));

	for (int i = 0; i < NumDevices; i++)
	{
		Device* device = &AllDevices[i];

		float randomScalar = (float)rand() / (float)(RAND_MAX);
		float randomMute = (float)rand() / (float)(RAND_MAX);
		BOOL mute = randomMute >= 0.5;
		float randomState = (float)rand() / (float)(RAND_MAX);
		BOOL state = randomState >= 0.5;


		float randomDefault = (float)rand() / (float)(RAND_MAX);
		float randomDefaultCommunication = (float)rand() / (float)(RAND_MAX);

		if (device->Info.State == DEVICE_STATE_ACTIVE)
		{
			device->AudioEndpointVolume->SetMasterVolumeLevelScalar(randomScalar, &GUID_NULL);
			device->AudioEndpointVolume->SetMute(mute, &GUID_NULL);
			if (randomDefault < 0.25)
				SetDefaultDevicesWhere(ERole::eMultimedia, device->Info.DataFlow, device->Info.Name);
			if (randomDefaultCommunication < 0.25)
				SetDefaultDevicesWhere(ERole::eCommunications, device->Info.DataFlow, device->Info.Name);
		}

		PolicyConfig->SetEndpointVisibility(device->Info.Id, state);
	}
}

static void SetAstroDevices()
{
	wchar_t* astroGame = L"*Astro*Game*";
	wchar_t* astroVoice = L"*Astro*Voice*";

	SetDevicesWhere(1.0, FALSE, astroGame, false);
	SetDevicesWhere(1.0, FALSE, astroVoice, false);

	if (SetDefaultDevicesWhere(ERole::eMultimedia, EDataFlow::eRender, astroGame))
		printf("Set Astro Default Playback Device\n");
	else
		printf("Unable to find Astro Playback Device. Did not set Default Playback Device.\n");

	if (SetDefaultDevicesWhere(ERole::eCommunications, EDataFlow::eRender, astroVoice))
		printf("Set Astro Default Playback Communication Device\n");
	else
		printf("Unable to find Astro Playback Communication Device. Did not set Default Playback Communication Device.\n");

	if (SetDefaultDevicesWhere(ERole::eMultimedia, EDataFlow::eCapture, astroVoice))
		printf("Set Astro Default Recording Device\n");
	else
		printf("Unable to find Astro Recording Device. Did not set Default Recording Device.\n");

	if (SetDefaultDevicesWhere(ERole::eCommunications, EDataFlow::eCapture, astroVoice))
		printf("Set Astro Default Recording Communication Device\n");
	else
		printf("Unable to find Astro Recording Communication Device. Did not set Default Recording Communication Device.\n");
}

static void SetTCHeliconDevices()
{
	wchar_t* TCsystem = L"*System*TC-Helicon*";
	wchar_t* TCchat = L"*Chat*TC-Helicon*";
	wchar_t* TCmic = L"*Mic*TC-Helicon*";

	SetDevicesWhere(1.0, FALSE, TCsystem, false);
	SetDevicesWhere(1.0, FALSE, TCchat, false);
	SetDevicesWhere(1.0, FALSE, TCmic, false);

	if (SetDefaultDevicesWhere(ERole::eMultimedia, EDataFlow::eRender, TCsystem))
		printf("Set TC-Helicon Default Playback Device\n");
	else
		printf("Unable to find TC-Helicon Playback Device. Did not set Default Playback Device.\n");

	if (SetDefaultDevicesWhere(ERole::eCommunications, EDataFlow::eRender, TCchat))
		printf("Set TC-Helicon Default Playback Communication Device\n");
	else
		printf("Unable to find TC-Helicon Playback Communication Device. Did not set Default Playback Communication Device.\n");

	if (SetDefaultDevicesWhere(ERole::eMultimedia, EDataFlow::eCapture, TCmic))
		printf("Set TC-Helicon Default Recording Device\n");
	else
		printf("Unable to find TC-Helicon Recording Device. Did not set Default Recording Device.\n");

	if (SetDefaultDevicesWhere(ERole::eCommunications, EDataFlow::eCapture, TCmic))
		printf("Set TC-Helicon Default Recording Communication Device\n");
	else
		printf("Unable to find TC-Helicon Recording Communication Device. Did not set Default Recording Communication Device.\n");


}

static void SetNDIDevices()
{
	wchar_t* NDIWebcam = L"*NDI*Webcam*";

	SetDevicesWhere(1.0, FALSE, NDIWebcam, false);

	if (SetDefaultDevicesWhere(ERole::eMultimedia, EDataFlow::eRender, NDIWebcam))
		printf("Set NDI Webcam as Default Playback Device\n");
	else
		printf("Unable to find NDI Webcam Playback Device. Did not set Default Playback Device.\n");

	if (SetDefaultDevicesWhere(ERole::eCommunications, EDataFlow::eRender, NDIWebcam))
		printf("Set NDI Webcam as Default Playback Communication Device\n");
	else
		printf("Unable to find NDI Webcam Playback Communication Device. Did not set Default Playback Communication Device.\n");

	if (SetDefaultDevicesWhere(ERole::eMultimedia, EDataFlow::eCapture, NDIWebcam))
		printf("Set NDI Webcam as Default Recording Device\n");
	else
		printf("Unable to find NDI Webcam Recording Device. Did not set Default Recording Device.\n");

	if (SetDefaultDevicesWhere(ERole::eCommunications, EDataFlow::eCapture, NDIWebcam))
		printf("Set NDI Webcam as Default Recording Communication Device\n");
	else
		printf("Unable to find NDI Webcam Recording Communication Device. Did not set Default Recording Communication Device.\n");


}

static void SetRealtekDevices()
{
	wchar_t* RealtekSpeakers = L"*Speakers*Realtek*";
	wchar_t* RealtekDigital = L"*Digital*Realtek*";
	wchar_t* RealtekMicrophone = L"*Microphone*Realtek*";
	wchar_t* RealtekLineIn = L"*Line*In*Realtek*";
	wchar_t* RealtekStereoMix = L"*Stereo*Mix*Realtek*";

	SetDevicesWhere(1.0, FALSE, RealtekSpeakers, false);
	SetDevicesWhere(1.0, FALSE, RealtekDigital, false);
	SetDevicesWhere(1.0, FALSE, RealtekMicrophone, false);
	SetDevicesWhere(1.0, FALSE, RealtekLineIn, false);
	SetDevicesWhere(1.0, FALSE, RealtekStereoMix, false);

	if (SetDefaultDevicesWhere(ERole::eMultimedia, EDataFlow::eRender, RealtekSpeakers))
		printf("Set RealtekSpeakers as Default Playback Device\n");
	else
		printf("Unable to find RealtekSpeakers Playback Device. Did not set Default Playback Device.\n");

	if (SetDefaultDevicesWhere(ERole::eCommunications, EDataFlow::eRender, RealtekSpeakers))
		printf("Set RealtekSpeakers as Default Playback Communication Device\n");
	else
		printf("Unable to find RealtekSpeakers Playback Communication Device. Did not set Default Playback Communication Device.\n");

	if (SetDefaultDevicesWhere(ERole::eMultimedia, EDataFlow::eCapture, RealtekMicrophone))
		printf("Set RealtekMicrophone as Default Recording Device\n");
	else
		printf("Unable to find RealtekMicrophone Recording Device. Did not set Default Recording Device.\n");

	if (SetDefaultDevicesWhere(ERole::eCommunications, EDataFlow::eCapture, RealtekMicrophone))
		printf("Set RealtekMicrophone as Default Recording Communication Device\n");
	else
		printf("Unable to find RealtekMicrophone Recording Communication Device. Did not set Default Recording Communication Device.\n");



}

static char* BoolToString(BOOL _bool)
{
	if (_bool)
		return "Muted";
	return "Unmuted";
}

static char* DwordToString(DWORD _dword)
{
	if (_dword == DEVICE_STATE_ACTIVE)
		return "Active";
	if (_dword == DEVICE_STATE_DISABLED)
		return "Disabled";
	if (_dword == DEVICE_STATE_UNPLUGGED)
		return "Unplugged";
	else
		return "Unknown";
}

static void PrintInfo(DeviceInfo* info)
{
	char* flowString;
	if (info->DataFlow == EDataFlow::eCapture)
		flowString = "Recording";
	if (info->DataFlow == EDataFlow::eRender)
		flowString = "Playback";

	int widthVeryShort = 3;
	int widthShort = 7;
	int widthLong = 50;
	int precisionInt = 0;
	int precisionFloat = 2;

	printf("Name: %*ls", widthLong, info->Name);
	printf("%-*s", widthVeryShort, "");

	printf("Scalar: %*.*f ", widthVeryShort, precisionInt, info->VolumeScalar * 100);
	printf("%-*s", widthVeryShort, "");

	printf("Level: %*.*f ", widthShort - 1, precisionFloat, info->VolumeLevel);
	printf("%-*s", widthVeryShort, "");

	printf("%*s ", widthShort, BoolToString(info->IsMute));
	//printf("%-*s", widthVeryShort, "");

	printf("%*s ", widthShort + 4, DwordToString(info->State));
	printf("%-*s", widthVeryShort, "");

	if (info->DataFlow == EDataFlow::eRender)
	{
		if (info->IsDefaultPlayback)
			printf("*");
		if (info->IsDefaultCommunicationPlayback)
			printf("**");
	}

	if (info->DataFlow == EDataFlow::eCapture)
	{
		if (info->IsDefaultRecording)
			printf("*");
		if (info->IsDefaultCommunicationRecording)
			printf("**");
	}

	printf("\n");
}

static void PrintAllDevices()
{
	printf("------------ Playback Devices ------------\n");
	for (int i = 0; i < NumDevices; i++) {
		if (AllDevices[i].Info.DataFlow != EDataFlow::eRender)
			continue;

		PrintInfo(&AllDevices[i].Info);
	}

	printf("\n------------ Recording Devices ------------\n");
	for (int i = 0; i < NumDevices; i++) {
		if (AllDevices[i].Info.DataFlow != EDataFlow::eCapture)
			continue;

		PrintInfo(&AllDevices[i].Info);
	}
}

static void SaveInfo(DeviceInfo* info, FILE* config)
{
	fprintf(config, "Name: %ls\n", info->Name);
	fprintf(config, "VolumeScalar: %f\n", info->VolumeScalar);
	fprintf(config, "VolumeLevel: %f\n", info->VolumeLevel);
	fprintf(config, "Mute: %i\n", info->IsMute);
	fprintf(config, "DefaultPlayback: %i\n", info->IsDefaultPlayback);
	fprintf(config, "DefaultPlaybackCommunication: %i\n", info->IsDefaultCommunicationPlayback);
	fprintf(config, "DefaultRecording: %i\n", info->IsDefaultRecording);
	fprintf(config, "DefaultRecordingCommunication: %i\n", info->IsDefaultCommunicationRecording);
	fprintf(config, "State: %i\n", info->State);
	//fprintf(config, "\n");
}

static void SaveAllInfo() {
	printf("Saving all audio device information... \n");

	FILE* config;
	config = (fopen("D:\\CAudioDevices\\config.txt", "w"));
	if (config == NULL)
	{
		printf("Error!");
		exit(1);
	}

	for (int i = 0; i < NumDevices; i++)
	{
		SaveInfo(&AllDevices[i].Info, config);
	}

	fclose(config);
}

static void LoadInfo2(FILE* config)
{
	Device* workingDevice = NULL;
	char line[100];

	int headerLength;
	char header[100];

	wchar_t wline[100];
	float lineFloat;
	bool lineBool;
	int lineInt;

	while (!feof(config)) {
		fgets(line, 100, config);

		// convert char to wide char and remove \n
		int length = mbstowcs(wline, line, 100);
		wline[length - 1] = NULL;

		for (int i = 0; i < length; i++)
		{
			if (line[i] != ' ')
				header[i] = line[i];
			else
			{
				header[i] = NULL;
				break;
			}
		}

		// if this config row is a "Name: ..."
		// Change the working device
		if (strcmp("Name:", header) == 0)
		{
			for (int i = 0; i < NumDevices; i++)
			{
				if (lstrcmpW(AllDevices[i].Info.Name, wline + 6) == 0)
				{
					workingDevice = &AllDevices[i];
					workingDevice->Device->GetState(&workingDevice->Info.State);
					break;
				}
				else
				{
					workingDevice = NULL;
				}
			}
		}
		
		if (strcmp("VolumeScalar:", header) == 0)
		{
			lineFloat = atof(line + 14);
			if (workingDevice->Info.State == DEVICE_STATE_ACTIVE)
			{
				HRESULT test = workingDevice->AudioEndpointVolume->SetMasterVolumeLevelScalar(lineFloat, &GUID_NULL);
				if (SUCCEEDED(test))
				{
					printf("Set volume\n");
				}
				else
				{
					printf("Didn't set volume\n");
				}
			}
			else
			{

			}
		}
	}
}

static void LoadInfo(Device* device, FILE* config)
{
	char line[100];
	wchar_t wline[100];
	float lineFloat;
	bool lineBool;
	int lineInt;

	while(!feof(config)){

		fgets(line, 100, config);
		//printf(line);

		// convert char to wide char and remove \n
		int length = mbstowcs(wline, line, 100);
		wline[length - 1] = NULL;

		// check name from file to names of audio device
		if (lstrcmpW(device->Info.Name, wline + 6) == 0)
		{
			PolicyConfig->SetEndpointVisibility(device->Info.Id, true);
			device->Device->GetState(&device->Info.State);

			// volumeScalar
			fgets(line, 100, config);
			lineFloat = atof(line + 14);

			if (device->Info.State == DEVICE_STATE_ACTIVE)
				device->AudioEndpointVolume->SetMasterVolumeLevelScalar(lineFloat, &GUID_NULL);
			
			// VolumeLevel
			fgets(line, 100, config);
			lineFloat = atof(line + 13);

			if (device->Info.State == DEVICE_STATE_ACTIVE) 
				device->AudioEndpointVolume->SetMasterVolumeLevel(lineFloat, &GUID_NULL);
			
			// mute
			fgets(line, 100, config);
			lineBool = atoi(line + 6);

			if (device->Info.State == DEVICE_STATE_ACTIVE) 
				device->AudioEndpointVolume->SetMute(lineBool, &GUID_NULL);

			// default playback
			fgets(line, 100, config);
			lineBool = atoi(line + 17);

			if (device->Info.State == DEVICE_STATE_ACTIVE && lineBool == 1)
				PolicyConfig->SetDefaultEndpoint(device->Info.Id, eConsole);

			// default playbackcomm
			fgets(line, 100, config);
			lineBool = atoi(line + 30);

			if (device->Info.State == DEVICE_STATE_ACTIVE && lineBool == 1)
				PolicyConfig->SetDefaultEndpoint(device->Info.Id, eCommunications);
			
			// default recording
			fgets(line, 100, config);
			lineBool = atoi(line + 18);

			if (device->Info.State == DEVICE_STATE_ACTIVE && lineBool == 1)
				PolicyConfig->SetDefaultEndpoint(device->Info.Id, eConsole);

			// default recordingcomm
			fgets(line, 100, config);
			lineBool = atoi(line + 31);

			if (device->Info.State == DEVICE_STATE_ACTIVE && lineBool == 1)
				PolicyConfig->SetDefaultEndpoint(device->Info.Id, eCommunications);

			// state
			fgets(line, 100, config);
			lineInt = atoi(line + 7);
			if (lineInt == 1)
				PolicyConfig->SetEndpointVisibility(device->Info.Id, true);
			if (lineInt == 2)
				PolicyConfig->SetEndpointVisibility(device->Info.Id, false);
			//if (lineInt == 8)
			//	PolicyConfig->SetEndpointVisibility(device->Info.Id, true);


			fgets(line, 100, config);
			//fgets(line, 100, config);
			//printf("\n");
		}
		else
		{
			// do nothing
			//printf("No match.\n\n");
		}
	}

	
}

static void LoadAllInfo() {
	printf("Loading all audio device information... \n");

	FILE* config;
	config = (fopen("D:\\CAudioDevices\\config.txt", "r"));
	if (config == NULL)
	{
		printf("Error!");
		exit(1);
	}

	for (size_t i = 0; i < NumDevices; i++) {
		rewind(config);
		LoadInfo(&AllDevices[i], config);
	}
		
	
	fclose(config);
}

int main(int numArguments, char* arguments[])
{
	CoInitialize(NULL);
	InitializeAndPopulateAllDevices();

	wchar_t clause[100];
	bool invalid = false;

	if (numArguments == 2)
	{
		if (strcmp(arguments[1], "-e") == 0)
		{
			EnableAllDevices();
		}
		else if (strcmp(arguments[1], "-d") == 0)
		{
			DisableAllDevices();
		}
		else if (strcmp(arguments[1], "-l") == 0)
		{
			PrintAllDevices();
		}
		else if (strcmp(arguments[1], "-save") == 0)
		{
			SaveAllInfo();
		}
		else if (strcmp(arguments[1], "-load") == 0)
		{
			LoadAllInfo();
		}
		else if (strcmp(arguments[1], "-r") == 0)
		{
			RandomizeAllDevices();
		}
		else if (strcmp(arguments[1], "-Astro") == 0)
		{
			SetAstroDevices();
		}
		else if (strcmp(arguments[1], "-TC") == 0)
		{
			SetTCHeliconDevices();
		}
		else if (strcmp(arguments[1], "-NDI") == 0)
		{
			SetNDIDevices();
		}
		else if (strcmp(arguments[1], "-Realtek") == 0)
		{
			SetRealtekDevices();
		}
		else
		{
			invalid = true;
		}
	}
	else if (numArguments == 3)
	{
		if (strcmp(arguments[1], "-u") == 0)
		{
			// Unmute all matching devices
			swprintf(clause, 100, L"%hs", arguments[2]);
			SetDevicesWhere(1.0, FALSE, clause, false);
		}
		else if (strcmp(arguments[1], "-m") == 0)
		{
			// Mute all matching devices
			swprintf(clause, 100, L"%hs", arguments[2]);
			SetDevicesWhere(0.0, TRUE, clause, false);
		}
		else if (strcmp(arguments[1], "-un") == 0)
		{
			// Unmute all non-matching devices
			swprintf(clause, 100, L"%hs", arguments[2]);
			SetDevicesWhere(1.0, FALSE, clause, true);
		}
		else if (strcmp(arguments[1], "-mn") == 0)
		{
			// Mute all non-matching devices
			swprintf(clause, 100, L"%hs", arguments[2]);
			SetDevicesWhere(0.0, TRUE, clause, true);
		}
		else
			invalid = true;
	}
	else
	{
		invalid = true;
	}

	if (invalid)
	{
		printf("\nUnknown or missing arguments.\n\n");
		printf(" -l\t\tList all playback and recording devices.\n");
		printf(" -e\t\tEnable all playback and recording devices.\n");
		printf(" -d\t\tDisable all playback and recording devices.\n");
		printf(" -save\t\tSave all audio device info to a file.\n");
		printf(" -load\t\tLoad all audio device info from a file.\n");

		printf("\n");
		printf(" -r\t\tRandomize mute, volume, default, and default communication devices.\n");
		printf("\n");
		printf(" -u <clause>\tUnmute and max volume all devices matching given clause.\n");
		printf(" -un <clause>\tUnmute and max volume all devices NOT matching given clause.\n");
		printf("\n");
		printf(" -m <clause>\tMute and 0 volume all devices matching given clause.\n");
		printf(" -mn <clause>\tMute and 0 volume all devices NOT matching given clause.\n");
		printf("\n");
		printf(" -Astro\t\tSet Default devices to expected Astro devices.\n");
		printf(" -Realtek\tSet Default devices to expected Realtek devices.\n");
		printf(" -NDI\t\tSet Default devices to expected NDI devices.\n");
		printf(" -TC\t\tSet Default devices to expected TC-Helicon devices.\n");
		printf("\n* -> Default Device\n");
		printf("** -> Default Communication Device\n");
		printf("\n");
	}



	return 0;
}