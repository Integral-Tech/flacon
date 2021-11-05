/* BEGIN_COMMON_COPYRIGHT_HEADER
 * (c)LGPL2+
 *
 * Flacon - audio File Encoder
 * https://github.com/flacon/flacon
 *
 * Copyright: 2012-2013
 *   Alexander Sokoloff <sokoloff.a@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.

 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.

 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * END_COMMON_COPYRIGHT_HEADER */

#include "encoder.h"
#include "resampler.h"
#include "profiles.h"
#include "settings.h"

#include <QFileInfo>
#include <QDir>
#include <QDebug>
#include <QLoggingCategory>

namespace {
Q_LOGGING_CATEGORY(LOG, "Converter")
}

using namespace Conv;

const quint64 MIN_BUF_SIZE = 4 * 1024;
const quint64 MAX_BUF_SIZE = 1024 * 1024;

/************************************************
 *
 ************************************************/
Encoder::Encoder(QObject *parent) :
    Worker(parent)
{
}

/************************************************
 *
 ************************************************/
void Encoder::check(QProcess *process)
{
    if (process->exitCode() != 0) {
        qWarning() << "Encoder command failed: " << debugProgramArgs(process->program(), process->arguments());
        QString msg = tr("Encoder error:\n") + "<pre>" + QString::fromLocal8Bit(process->readAllStandardError()) + "</pre>";
        emit    error(track(), msg);
    }
}

/************************************************
 *
 ************************************************/
void Encoder::runOneProcess(QProcess *process)
{
    connect(process, &QProcess::bytesWritten, this, &Encoder::processBytesWritten);

    process->start();
    readInputFile(process);
    process->closeWriteChannel();
    process->waitForFinished(-1);
    check(process);
}

/************************************************
 *
 ************************************************/
void Encoder::runTwoProcess(QProcess *resampler, QProcess *encoder)
{
    resampler->setStandardOutputProcess(encoder);

    connect(resampler, &QProcess::bytesWritten, this, &Encoder::processBytesWritten);

    resampler->start();
    encoder->start();

    readInputFile(resampler);
    resampler->closeWriteChannel();
    resampler->waitForFinished(-1);
    check(resampler);

    encoder->closeWriteChannel();
    encoder->waitForFinished(-1);
    check(encoder);
}

/************************************************

 ************************************************/
void Encoder::run()
{
    emit trackProgress(track(), TrackState::Encoding, 0);

    const qint8 COPY_FILE = 0, RESAMPLE = 1, ENCODE = 2, RESAMPLE_ENCODE = 3;

    QProcess resampler;
    QProcess encoder;
    qint8    mode = COPY_FILE;

    if (profile().formatId() != "WAV") {
        QStringList args = programArgs();
        QString     prog = args.takeFirst();

        qCDebug(LOG) << "Start encoder:" << debugProgramArgs(prog, args);

        encoder.setProgram(prog);
        encoder.setArguments(args);
#ifdef MAC_BUNDLE
        encoder.setEnvironment(QStringList("LANG=en_US.UTF-8"));
#endif
        mode += ENCODE;
    }

    const InputAudioFile &audio = mTrack.audioFile();

    int bps  = bitsPerSample(audio);
    int rate = sampleRate(audio);

    if (bps != audio.bitsPerSample() || rate != audio.sampleRate()) {
        QString outFile;
        if (mode == COPY_FILE)
            outFile = mOutFile; // Input file already WAV, so for WAV output format we just rename file.
        else
            outFile = "-"; // Write to STDOUT

        QStringList args = Resampler::args(bps, rate, outFile);
        QString     prog = args.takeFirst();

        qCDebug(LOG) << "Start resampler:" << debugProgramArgs(prog, args);

        resampler.setProgram(prog);
        resampler.setArguments(args);
        mode += RESAMPLE;
    }

    switch (mode) {
        case COPY_FILE:
            runWav();
            return;

        case RESAMPLE:
            runOneProcess(&resampler);
            break;

        case ENCODE:
            runOneProcess(&encoder);
            break;

        case RESAMPLE_ENCODE:
            runTwoProcess(&resampler, &encoder);
            break;
    }

    deleteFile(mInputFile);
    emit trackReady(track(), outFile());
}

/************************************************

 ************************************************/
int Encoder::bitsPerSample(const InputAudioFile &audio) const
{
    return calcQuality(audio.bitsPerSample(), mProfile.bitsPerSample(), mProfile.outFormat()->maxBitPerSample());
}

/************************************************

 ************************************************/
int Encoder::sampleRate(const InputAudioFile &audio) const
{
    return calcQuality(audio.sampleRate(), mProfile.sampleRate(), mProfile.outFormat()->maxSampleRate());
}

/************************************************
 *
 ************************************************/
QString Encoder::programPath() const
{
    return Settings::i()->programName(programName());
}

/************************************************

 ************************************************/
void Encoder::processBytesWritten(qint64 bytes)
{
    mReady += bytes;
    int p = ((mReady * 100.0) / mTotal);
    if (p != mProgress) {
        mProgress = p;
        emit trackProgress(track(), TrackState::Encoding, mProgress);
    }
}

/************************************************

 ************************************************/
void Encoder::setProfile(const Profile &profile)
{
    mProfile = profile;
}

/************************************************

 ************************************************/
void Encoder::readInputFile(QProcess *process)
{
    QFile file(inputFile());
    if (!file.open(QFile::ReadOnly)) {
        emit error(track(), tr("I can't read %1 file", "Encoder error. %1 is a file name.").arg(inputFile()));
    }

    mProgress = -1;
    mTotal    = file.size();

    quint64    bufSize = qBound(MIN_BUF_SIZE, mTotal / 200, MAX_BUF_SIZE);
    QByteArray buf;

    while (!file.atEnd()) {
        buf = file.read(bufSize);
        process->write(buf);
    }
}

/************************************************

 ************************************************/
void Encoder::runWav()
{
    QFile srcFile(inputFile());
    bool  res = srcFile.rename(outFile());

    if (!res) {
        emit error(track(),
                   tr("I can't rename file:\n%1 to %2\n%3").arg(inputFile(), outFile(), srcFile.errorString()));
    }

    emit trackProgress(track(), TrackState::Encoding, 100);
    emit trackReady(track(), outFile());
}
