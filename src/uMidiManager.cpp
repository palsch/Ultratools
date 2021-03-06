#include "timebase.h"
#include "uMidiManager.h"
#include <QCoreApplication>
#include <QString>
#include <QDebug>

extern "C" {
#include <portmidi.h>
}


UMidiManager *UMidiManager::Instance;

UMidiManager *UMidiManager::getInstance()
{
    if (!Instance)
        Instance = new UMidiManager;
    return Instance;
}

static PmTimestamp midiTime(void *)
{
    return now();
}

static unsigned long extendTimestamp(PmTimestamp ts)
{
    if (sizeof(PmTimestamp) >= sizeof(unsigned long))
        return ts;

    unsigned long lts = now();
    return lts - (static_cast<PmTimestamp>(lts) - ts);
}

static QString portmidiError(PmError errnum)
{
    if (errnum >= 0)
        return QString();

    if (errnum != pmHostError)
        return QString(Pm_GetErrorText(errnum));

    char str[PM_HOST_ERROR_MSG_LEN];
    Pm_GetHostErrorText(str, sizeof(str));
    return QString(str);
}

UMidiManager::UMidiManager()
    : QObject(QCoreApplication::instance()), _initialized(false)
{
    PmError err;

    err = Pm_Initialize();
    if (err != pmNoError)
    {
        qDebug() << "Error initializing MIDI:" << portmidiError(err);
        return;
    }

    _initialized = true;

    for (int i = Pm_CountDevices(); i--;)
    {
        const PmDeviceInfo *info = Pm_GetDeviceInfo(i);
        PortMidiStream *stream;

        if (!info->input)
            continue;

        err = Pm_OpenInput(&stream, i, NULL, 64, midiTime, NULL);
        if (err != pmNoError)
        {
            qDebug() << "Error opening" << info->name << ":" << portmidiError(err);
            continue;
        }

        _streams.insert(i, static_cast<void *>(stream));

        err = Pm_SetFilter(stream, PM_FILT_REALTIME | PM_FILT_SYSTEMCOMMON | PM_FILT_AFTERTOUCH
                                 | PM_FILT_PROGRAM | PM_FILT_CONTROL | PM_FILT_PITCHBEND);
        if (err != pmNoError)
            qDebug() << "Failed to set MIDI filter:" << portmidiError(err);
    }

    if (!_streams.isEmpty())
    {
        connect(&_timer, SIGNAL(timeout()), this, SLOT(checkForEvents()));
        _timer.start(100);
    }
}

UMidiManager::~UMidiManager()
{
    if (!_initialized)
        return;

    foreach (void *s, _streams)
        Pm_Close(static_cast<PortMidiStream *>(s));

    Pm_Terminate();
}

void UMidiManager::checkForEvents()
{
    foreach (void *s, _streams)
    {
        PmEvent event;

        while (Pm_Read(static_cast<PortMidiStream *>(s), &event, 1) == 1)
        {
            switch (Pm_MessageStatus(event.message) & 0xF0)
            {
            case 0x90:
                if (Pm_MessageData2(event.message) != 0)
                {
                    emit noteOnEvent(extendTimestamp(event.timestamp),
                                     Pm_MessageData1(event.message));
                    break;
                }
            case 0x80:
                emit noteOffEvent(extendTimestamp(event.timestamp),
                                  Pm_MessageData1(event.message));
                break;
            }
        }
    }
}
