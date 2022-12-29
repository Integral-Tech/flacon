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

#include <QThread>
#include <QDebug>

#include "disc.h"
#include "converter.h"
#include "project.h"
#include "settings.h"
#include "splitter.h"
#include "encoder.h"
#include "discpipline.h"
#include "sox.h"
#include "cuecreator.h"

#include <iostream>
#include <math.h>
#include <QFileInfo>
#include <QDir>
#include <QLoggingCategory>

namespace {
Q_LOGGING_CATEGORY(LOG, "Converter")
}

using namespace Conv;

/************************************************

 ************************************************/
Converter::Converter(QObject *parent) :
    QObject(parent)
{
    qRegisterMetaType<Conv::ConvTrack>();
}

/************************************************
 *
 ************************************************/
Converter::~Converter()
{
}

/************************************************

 ************************************************/
void Converter::start(const Profile &profile)
{
    Jobs jobs;
    for (int d = 0; d < project->count(); ++d) {
        Job job;
        job.disc = project->disc(d);

        for (int t = 0; t < job.disc->count(); ++t)
            job.tracks << job.disc->track(t);

        jobs << job;
    }

    start(jobs, profile);
}

/************************************************
 *
 ************************************************/
void Converter::start(const Converter::Jobs &jobs, const Profile &profile)
{
    qCDebug(LOG) << "Start converter:" << jobs.length() << "\n"
                 << profile;
    qCDebug(LOG) << "Temp dir =" << Settings::i()->tmpDir();

    if (jobs.isEmpty()) {
        emit finished();
        return;
    }

    if (!validate(jobs, profile)) {
        emit finished();
        return;
    }

    bool ok;
    mThreadCount = Settings::i()->value(Settings::Encoder_ThreadCount).toInt(&ok);
    if (!ok || mThreadCount < 1) {
        mThreadCount = qMax(6, QThread::idealThreadCount());
    }

    qCDebug(LOG) << "Threads count" << mThreadCount;

    try {
        for (const Job &converterJob : jobs) {

            if (converterJob.tracks.isEmpty() || converterJob.disc->isEmpty()) {
                continue;
            }

            if (mValidator.diskHasErrors(converterJob.disc)) {
                continue;
            }

            mDiskPiplines << createDiscPipeline(profile, converterJob);
        }
    }
    catch (const FlaconError &err) {
        qCWarning(LOG) << "Can't start " << err.what();
        emit error(err.what());
        qDeleteAll(mDiskPiplines);
        mDiskPiplines.clear();
        emit finished();
    }

    mTotalProgressCounter.init(*this);
    connect(this, &Converter::trackProgress, &mTotalProgressCounter, &TotalProgressCounter::setTrackProgress, Qt::UniqueConnection);
    connect(&mTotalProgressCounter, &TotalProgressCounter::changed, this, &Converter::totalProgress, Qt::UniqueConnection);

    startThread();
    emit started();
}

/************************************************
 *
 ************************************************/
DiscPipeline *Converter::createDiscPipeline(const Profile &profile, const Converter::Job &converterJob)
{
    // Tracks ..............................
    ConvTracks resTracks;

    PreGapType preGapType = profile.isCreateCue() ? profile.preGapType() : PreGapType::Skip;

    for (const TrackPtrList &tracks : converterJob.disc->tracksByFileTag()) {

        // Pregap track ....................
        bool hasPregap =
                converterJob.tracks.contains(tracks.first()) && // We extract first track in Audio
                tracks.first()->cueIndex(1).milliseconds() > 0; // The first track don't start from zero second

        if (hasPregap && preGapType == PreGapType::ExtractToFile) {
            Track *firstTrack = tracks.first();

            Track pregapTrack = *firstTrack; // copy tags and all settings
            pregapTrack.setTag(TagId::TrackNum, QByteArray("0"));
            pregapTrack.setTitle("(HTOA)");

            ConvTrack track(pregapTrack);
            track.setPregap(true);

            resTracks << track;
        }

        // Tracks ..........................
        for (int i = 0; i < tracks.count(); ++i) {
            const Track *t = tracks.at(i);

            if (!converterJob.tracks.contains(t)) {
                continue;
            }

            ConvTrack track(*t);
            track.setPregap(false);

            resTracks << track;
        }
    }

    QString wrkDir = workDir(converterJob.tracks.first());

    DiscPipeline *pipeline = new DiscPipeline(profile, converterJob.disc, resTracks, wrkDir, this);

    connect(pipeline, &DiscPipeline::readyStart, this, &Converter::startThread);
    connect(pipeline, &DiscPipeline::threadFinished, this, &Converter::startThread);
    connect(pipeline, &DiscPipeline::trackProgressChanged, this, &Converter::trackProgress);

    return pipeline;
}

/************************************************

 ************************************************/
bool Converter::isRunning()
{
    foreach (DiscPipeline *pipe, mDiskPiplines) {
        if (pipe->isRunning())
            return true;
    }

    return false;
}

/************************************************

 ************************************************/
void Converter::stop()
{
    if (!isRunning())
        return;

    foreach (DiscPipeline *pipe, mDiskPiplines) {
        pipe->stop();
    }
}

/************************************************

 ************************************************/
void Converter::startThread()
{
    int count         = mThreadCount;
    int splitterCount = qMax(1.0, ceil(count / 2.0));

    foreach (DiscPipeline *pipe, mDiskPiplines) {
        count -= pipe->runningThreadCount();
    }

    foreach (DiscPipeline *pipe, mDiskPiplines) {
        pipe->startWorker(&splitterCount, &count);
        if (count <= 0) {
            break;
        }
    }

    foreach (DiscPipeline *pipe, mDiskPiplines) {
        if (pipe->isRunning()) {
            return;
        }
    }

    emit finished();
}

/************************************************

 ************************************************/
bool Converter::validate(const Jobs &jobs, const Profile &profile)
{
    DiskList disks;
    for (const auto &j : jobs) {
        disks << j.disc;
    }

    mValidator.setDisks(disks);
    mValidator.setProfile(profile);

    QStringList errors = mValidator.converterErrors();

    if (errors.isEmpty()) {
        return true;
    }

    QString s;
    foreach (QString e, errors) {
        s += QString("<li style='margin-top: 4px;'> %1</li>").arg(e);
    }

    Messages::error(QString("<html>%1<ul>%2</ul></html>")
                            .arg(tr("Conversion is not possible:"))
                            .arg(s));

    return false;
}

/************************************************

 ************************************************/
QString Converter::workDir(const Track *track) const
{
    QString dir = Settings::i()->tmpDir();
    if (dir.isEmpty()) {
        dir = QFileInfo(track->resultFilePath()).dir().absolutePath();
    }
    return dir + "/tmp";
}
