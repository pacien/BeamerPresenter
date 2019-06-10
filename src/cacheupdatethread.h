/*
 * This file is part of BeamerPresenter.
 * Copyright (C) 2019  stiglers-eponym

 * BeamerPresenter is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.

 * BeamerPresenter is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with BeamerPresenter. If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef CACHEUPDATETHREAD_H
#define CACHEUPDATETHREAD_H

#include <QThread>
#include "pagewidget.h"
#include "externalrenderer.h"

enum Renderer {
    poppler = 0,
    custom = 1,
};

class CacheUpdateThread : public QThread
{
    Q_OBJECT

private:
    Renderer mode = Renderer::poppler;
    QString renderCommand = "";
    QString presFileName = "";
    QString noteFileName = "";
    QStringList renderArguments = {};
    PageWidget const* presLabel;
    PageWidget const* noteLabel;
    PageWidget const* smallLabel;
    Poppler::Page const* presPage;
    Poppler::Page const* notePage;
    PagePart pagePart = FullPage;

public:
    CacheUpdateThread(QObject* parent=nullptr) : QThread(parent) {}
    void setCustomRenderer(QString const& renderCommand, QString const& presFileName, QString const& noteFileName, QStringList const& renderArguments, Renderer renderer = Renderer::custom);
    void setLabels(PageWidget const* pres, PageWidget const* note, PageWidget const* small);
    void setPages(Poppler::Page const* pres, Poppler::Page const* note);
    void setRenderer(Renderer renderer) {mode=renderer;}
    bool hasRenderCommand() {return !renderCommand.isEmpty();}
    void setPagePart(PagePart const part) {pagePart=part;}
    void run() override;

signals:
    void resultsReady(QByteArray const* pres, QByteArray const* note, QByteArray const* small, int const index);
};

#endif // CACHEUPDATETHREAD_H
