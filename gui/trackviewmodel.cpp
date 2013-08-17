/* BEGIN_COMMON_COPYRIGHT_HEADER
 * (c)LGPL2+
 *
 * Flacon - audio File Encoder
 * https://github.com/SokoloffA/flacon
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


#include "trackviewmodel.h"
#include "trackview.h"
#include "project.h"
#include "disk.h"
#include "settings.h"

#include <QDebug>
#include <QSet>


/************************************************

 ************************************************/
TrackViewModel::TrackViewModel(TrackView *parent) :
    QAbstractItemModel(parent),
    mView(parent)
{
    connect(project, SIGNAL(diskChanged(Disk*)), this, SLOT(diskDataChanged(Disk*)));
    connect(project, SIGNAL(trackChanged(int,int)), this, SLOT(trackDataChanged(int,int)));
    connect(project, SIGNAL(layoutChanged()), this, SIGNAL(layoutChanged()));
    connect(project, SIGNAL(beforeRemoveDisk(Disk*)), this, SLOT(beforeRemoveDisk(Disk*)));
    connect(project, SIGNAL(afterRemoveDisk()), this, SLOT(afterRemoveDisk()));

    connect(project, SIGNAL(trackProgress(const Track*)), this, SLOT(trackProgressChanged(const Track*)));

    //connect(project, SIGNAL("downloadStarted(int)"), self._downloadStarted)
    //self.connect(project, SIGNAL("downloadFinished(int)"), self._downloadFinished)
}


/************************************************

 ************************************************/
QVariant TrackViewModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return QVariant();

    switch(section)
    {
    case TrackView::ColumnTracknum:   return QVariant(tr("Track",   "Table header."));
    case TrackView::ColumnTitle:      return QVariant(tr("Title",   "Table header."));
    case TrackView::ColumnArtist:     return QVariant(tr("Artist",  "Table header."));
    case TrackView::ColumnAlbum:      return QVariant(tr("Album",   "Table header."));
    case TrackView::ColumnComment:    return QVariant(tr("Comment", "Table header."));
    case TrackView::ColumnFileName:   return QVariant(tr("File",    "Table header."));
    }
    return QVariant();
}




/************************************************

 ************************************************/
QModelIndex TrackViewModel::index(int row, int column, const QModelIndex &parent) const
{
    if (!hasIndex(row, column, parent))
        return QModelIndex();


    if (parent.internalPointer() == project)
        return createIndex(row, column, project->disk(row));

    QObject *obj = static_cast<QObject*>(parent.internalPointer());
    Disk *disk = qobject_cast<Disk*>(obj);
    if(disk)
        return createIndex(row, column, disk->track(row));
    else
        return createIndex(row, column, project->disk(row));


    return QModelIndex();
}


/************************************************

 ************************************************/
QModelIndex TrackViewModel::index(const Disk *disk, int col) const
{
    const int diskNum = project->indexOf(disk);
    if (diskNum > -1 && diskNum < rowCount(QModelIndex()))
        return index(diskNum, col, QModelIndex());
    else
        return QModelIndex();
}


/************************************************

 ************************************************/
QModelIndex TrackViewModel::index(const Track *track, int col) const
{
    QModelIndex diskIndex = index(track->disk());
    if (!diskIndex.isValid())
        return QModelIndex();

    int trackNum = track->index();
    if (trackNum > -1 && trackNum < rowCount(diskIndex))
        return index(trackNum, col, diskIndex);
    else
        return QModelIndex();
}


/************************************************

 ************************************************/
QModelIndex TrackViewModel::parent(const QModelIndex &child) const
{
    if (!child.isValid())
        return QModelIndex();

    QObject *obj = static_cast<QObject*>(child.internalPointer());
    Track *track = qobject_cast<Track*>(obj);
    if (track)
    {
        int row = project->indexOf(track->disk());
        return index(row, 0, QModelIndex());
    }

    return QModelIndex();
}


/************************************************

 ************************************************/
QVariant TrackViewModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return QVariant();

    Track *track = trackByIndex(index);
    if (track)
        return trackData(track, index, role);

    Disk *disk = diskByIndex(index);
    if(disk)
        return diskData(disk, index, role);

    return QVariant();
}


/************************************************

 ************************************************/
bool TrackViewModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid())
        return false;

    if (role != Qt::EditRole)
        return false;


    QList<Track*> tracks = view()->selectedTracks();
    foreach(Track *track, tracks)
    {
        switch (index.column())
        {
        case TrackView::ColumnTitle:
            track->setTitle(value.toString());
            break;

        case TrackView::ColumnArtist:
            track->setArtist(value.toString());
            break;

        case TrackView::ColumnAlbum:
            track->setAlbum(value.toString());
            break;

        case TrackView::ColumnComment:
            track->setComment(value.toString());
            break;
        }
    }

    return true;
}


/************************************************

 ************************************************/
QVariant TrackViewModel::trackData(const Track *track, const QModelIndex &index, int role) const
{
    // Display & Edit :::::::::::::::::::::::::::::::::::
    if (role == Qt::DisplayRole || role == Qt::EditRole )
    {
        switch (index.column())
        {
        case TrackView::ColumnTracknum:
            return QVariant(QString("%1").arg(track->trackNum(), 2, 10, QChar('0')));

        case TrackView::ColumnTitle:
            return QVariant(track->title());

        case TrackView::ColumnArtist:
            return QVariant(track->artist());

        case TrackView::ColumnAlbum:
            return QVariant(track->album());

        case TrackView::ColumnComment:
            return QVariant(track->comment());

        case TrackView::ColumnFileName:
            return QVariant(track->resultFileName());
        }

        return QVariant();
    }

    // ToolTip ::::::::::::::::::::::::::::::::::::::::::
    if (role == Qt::ToolTipRole)
    {
        switch (index.column())
        {
        case TrackView::ColumnFileName:
            return QVariant(track->resultFilePath());

        default:
            return QVariant();
        }
    }

    // StatusPercent ::::::::::::::::::::::::::::::::::::
//    if (role == StatusPercentRole)
//    {
//     //   return track.progress()
//    }

//    // Status :::::::::::::::::::::::::::::::::::::::::::
//    if (role == StatusRole)
//    {
//    //    return track.status();
//    }

    return QVariant();
}


/************************************************

 ************************************************/
QVariant TrackViewModel::diskData(const Disk *disk, const QModelIndex &index, int role) const
{
    // Display & Edit :::::::::::::::::::::::::::::::::::
    if (role == Qt::DisplayRole || role == Qt::EditRole )
    {
        if (!disk->count())
            return QVariant();

        QSet<QString> values;

        switch (index.column())
        {
        case TrackView::ColumnTitle:
            for (int i=0; i<disk->count(); ++i)
                values << disk->track(i)->title();
            break;

        case TrackView::ColumnArtist:
            for (int i=0; i<disk->count(); ++i)
                values << disk->track(i)->artist();
            break;

        case TrackView::ColumnAlbum:
            for (int i=0; i<disk->count(); ++i)
                values << disk->track(i)->album();
            break;
        }

        if (values.count() > 1)
        {
            return QVariant(tr("Multiple values"));
        }
        else if (values.count() == 1)
        {
            return QVariant(*(values.begin()));
        }

        return QVariant();
    }

    // ToolTip ::::::::::::::::::::::::::::::::::::::::::
    if (role == Qt::ToolTipRole)
    {
        QString s;
        if (!disk->canConvert(&s))
            return QVariant(tr("Conversion is not possible.\n%1").arg(s));
        else
            return QVariant();

    }

//# StatusPercent ::::::::::::::::::::::::::::::::::::
//elif (role == self.StatusPercentRole):
//    try:
//        return QVariant(self._downloadsStates[index.row()])
//    except KeyError:
//        return QVariant()

//# Download pixmap ::::::::::::::::::::::::::::::::::
//elif role == self.DownloadPxmapRole:
//    try:
//        self._downloadsStates[index.row()]
//        return QVariant(self._downloadMovie.currentPixmap())
//    except KeyError:
//        return QVariant()


    return QVariant();

}


/************************************************

 ************************************************/
int TrackViewModel::columnCount(const QModelIndex &parent) const
{
    return TrackView::ColumnCount;
}


/************************************************

 ************************************************/
int TrackViewModel::rowCount(const QModelIndex &parent) const
{
    if (!parent.isValid())
        return project->count();

    QObject *obj = static_cast<QObject*>(parent.internalPointer());
    Disk *disk = qobject_cast<Disk*>(obj);
    if(disk)
        return disk->count();

    return 0;
}


/************************************************

 ************************************************/
Qt::ItemFlags TrackViewModel::flags(const QModelIndex &index) const
{
    if (! index.isValid())
        return Qt::ItemIsEnabled;

    Qt::ItemFlags res = Qt::ItemIsEnabled | Qt::ItemIsSelectable;

    QObject *obj = static_cast<QObject*>(index.internalPointer());
    Track *track = qobject_cast<Track*>(obj);
    if (track)
    {
        switch (index.column())
        {
        case TrackView::ColumnTitle:
        case TrackView::ColumnArtist:
        case TrackView::ColumnAlbum:
        case TrackView::ColumnComment:
            res = res | Qt::ItemIsEditable;
            break;
        }

    }

    return res;
}


/************************************************

 ************************************************/
Disk *TrackViewModel::diskByIndex(const QModelIndex &index)
{
    QObject *obj = static_cast<QObject*>(index.internalPointer());
    return qobject_cast<Disk*>(obj);
}


/************************************************

 ************************************************/
Track *TrackViewModel::trackByIndex(const QModelIndex &index)
{
    QObject *obj = static_cast<QObject*>(index.internalPointer());
    return qobject_cast<Track*>(obj);
}


/************************************************

 ************************************************/
void TrackViewModel::trackProgressChanged(const Track *track)
{
    QModelIndex id = index(track, TrackView::ColumnPercent);
    emit dataChanged(id, id);
}


/************************************************

 ************************************************/
void TrackViewModel::diskDataChanged(Disk *disk)
{
    QModelIndex index1 = index(disk, 0);
    QModelIndex index2 = index(disk, TrackView::ColumnCount);
    emit dataChanged(index1, index2);
}


/************************************************

 ************************************************/
void TrackViewModel::trackDataChanged(int disk, int track)
{
    QModelIndex diskIndex = index(disk, 0, QModelIndex());
    QModelIndex index1 = index(track, 0, diskIndex);
    QModelIndex index2 = index(track, TrackView::ColumnCount, diskIndex);
    emit dataChanged(index1, index2);
}


/************************************************

 ************************************************/
void TrackViewModel::beforeRemoveDisk(Disk *disk)
{
    int n = project->indexOf(disk);
    beginRemoveRows(QModelIndex(), n, n);
}


/************************************************

 ************************************************/
void TrackViewModel::afterRemoveDisk()
{
    endRemoveRows();
}


