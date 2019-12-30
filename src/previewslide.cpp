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

#include "previewslide.h"

PreviewSlide::PreviewSlide(PdfDoc const * const document, int const pageNumber, QWidget* parent) :
    QWidget(parent),
    doc(document),
    cache(new CacheMap(document)),
    pageIndex(pageNumber)
{
    renderPage(pageNumber);
}

PreviewSlide::~PreviewSlide()
{
    // Clear all contents of the label.
    // This function is called when the document is reloaded or the program is closed and everything should be cleaned up.
    if (cache != nullptr)
        cache->clearCache();
    qDeleteAll(links);
    linkPositions.clear();
    links.clear();
    page = nullptr;
}

void PreviewSlide::renderPage(int pageNumber)
{
    if (pageNumber < 0)
        pageNumber = 0;
    else if (pageNumber >= doc->getDoc()->numPages())
        pageNumber = doc->getDoc()->numPages()-1;

    // Use overlay specific options
    // A page is called an overlay of the previously rendered page, if they have the same label.
    // This is also the case, if the same page is rendered again (e.g. because the window is resized).
    qDeleteAll(links);
    linkPositions.clear();
    links.clear();

    // Old cached images are useless if the label size has changed:
    if (size() != oldSize) {
        if (cache != nullptr)
            cache->clearCache();
        oldSize = size();
    }

    QPair<double,double> scale = basicRenderPage(pageNumber);
    pageIndex = pageNumber;

    // Show the page on the screen.
    // One could show the page in any case to make it slightly more responsive, but this can lead to a short interruption by a different image.
    // All operations before the next call to update() are usually very fast.
    update();

    // Collect link areas in pixels (positions relative to the lower left edge of the label)
    links = page->links();
    Q_FOREACH(Poppler::Link* link, links) {
        QRectF relative = link->linkArea();
        linkPositions.append(QRect(
                    shiftx+int(relative.x()*scale.first),
                    shifty+int(relative.y()*scale.second),
                    int(relative.width()*scale.first),
                    int(relative.height()*scale.second)
                ));
    }
}

QPair<double,double> PreviewSlide::basicRenderPage(int const pageNumber)
{
    // Set the new page and basic properties
    page = doc->getPage(pageNumber);
    QSizeF pageSize = page->pageSizeF();
    // This is given in point = inch/72 ≈ 0.353mm (Did they choose these units to bother programmers?)

    // Place the page as an image of the correct size at the correct position
    // The lower left corner of the image will be located at (shiftx, shifty)
    double pageHeight=pageSize.height(), pageWidth=pageSize.width();
    // The page image must be split if the beamer option "notes on second screen" is set.
    if (pagePart != FullPage)
        pageWidth /= 2;
    // Check it width or height is the limiting constraint for the size of the displayed slide and calculate the resolution
    // resolution is calculated in pixels per point = dpi/72.
    if (width() * pageHeight > height() * pageWidth) {
        // the width of the label is larger than required
        resolution = double(height()) / pageHeight;
        shiftx = qint16(width()/2 - resolution/2 * pageWidth);
        shifty = 0;
    }
    else {
        // the height of the label is larger than required
        resolution = double(width()) / pageWidth;
        shifty = qint16(height()/2 - resolution/2 * pageHeight);
        shiftx = 0;
    }
    if (cache != nullptr)
        cache->changeResolution(resolution);

    // Calculate the size of the image in pixels
    double scale_x=resolution*pageWidth, scale_y=resolution*pageHeight;
    // Adjustments if only parts of the page are shown:
    if (pagePart != FullPage) {
        scale_x *= 2;
        // If only the right half of the page will be shown, the position of the page (relevant for link positions) must be adjusted.
        if (pagePart == RightHalf)
            shiftx -= width();
    }

    if (pageIndex != pageNumber && cache != nullptr)
        pixmap = cache->getPixmap(pageNumber);
    return {scale_x, scale_y};
}

/*
qint64 PreviewSlide::updateCache(QPixmap const* pix, int const index)
{
    // Save the pixmap to (compressed) cache of page index and return the size of the compressed image.
    if (pix==nullptr || pix->isNull())
        return 0;
    // The image will be compressed and written to a QByteArray.
    QByteArray* bytes = new QByteArray();
    QBuffer buffer(bytes);
    buffer.open(QIODevice::WriteOnly);
    pix->save(&buffer, "PNG");
    cache[index] = bytes;
    return qint64(bytes->size());
}

qint64 PreviewSlide::updateCache(QByteArray const* bytes, int const index)
{
    // Write bytes to the cache of page index and return the size of bytes.
    if (bytes==nullptr || bytes->isNull() || bytes->isEmpty())
        return 0;
    else if (cache.contains(index))
        delete cache[index];
    cache[index] = bytes;
    return qint64(bytes->size());
}

qint64 PreviewSlide::updateCache(int const pageNumber)
{
    // Check whether the cachePage exists in cache. If yes, return 0.
    // Otherwise, render the given page using the internal renderer,
    // write the compressed image to cache and return the size of the compressed image.

    // Check whether the page exists in cache.
    if (cache.contains(pageNumber) || pageNumber<0 || pageNumber>doc->getDoc()->numPages())
        return 0;

    // Render the page to a pixmap
    Poppler::Page const* cachePage = doc->getPage(pageNumber);
    QImage image = cachePage->renderToImage(72*resolution, 72*resolution);
    // if pagePart != FullPage: Reduce the image to the relevant part.
    if (pagePart == LeftHalf)
        image = image.copy(0, 0, image.width()/2, image.height());
    else if (pagePart == RightHalf)
        image = image.copy(image.width()/2, 0, image.width()/2, image.height());

    // This check is repeated, because it could be possible that the cache is overwritten while the image is rendered.
    if (cache.contains(pageNumber))
        return 0;

    // Write the image in png format to a QBytesArray
    QByteArray* bytes = new QByteArray();
    QBuffer buffer(bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    cache[pageNumber] = bytes;
    return qint64(bytes->size());
}

QPixmap const PreviewSlide::getPixmap(int const pageNumber) const
{
    // Return a pixmap representing the current page.
    QPixmap pixmap;
    if (page == nullptr)
        return pixmap;
    if (cache.contains(pageNumber)) {
        // The page exists in cache. Use the cache instead of rendering it again.
        pixmap = getCache(pageNumber);
        QSizeF size = page->pageSizeF();
        int picwidth = int(resolution*size.width()), picheight = int(resolution*size.height());
        if (abs(picwidth-pixmap.width())<2 && abs(picheight-pixmap.height())<2)
            return pixmap;
    }
    Poppler::Page const* cachePage = doc->getPage(pageNumber);
    if (pagePart == FullPage)
        pixmap = QPixmap::fromImage(cachePage->renderToImage(72*resolution, 72*resolution));
    else {
        QImage image = cachePage->renderToImage(72*resolution, 72*resolution);
        if (pagePart == LeftHalf)
            pixmap = QPixmap::fromImage(image.copy(0, 0, image.width()/2, image.height()));
        else
            pixmap = QPixmap::fromImage(image.copy(image.width()/2, 0, image.width()/2, image.height()));
    }
    return pixmap;
}

QPixmap const PreviewSlide::getCache(int const index) const
{
    // Get a pixmap from cache.
    QPixmap pixmap;
    if (cache.contains(index)) {
        pixmap.loadFromData(*cache[index], "PNG");
        // If an external renderer is used, cached images always show the full page.
        // But if pagePart != FullPage, only one half of the image should be shown.
        if (pagePart != FullPage) {
            // The cached pixmap might show both notes and presentation.
            // Check the width to decide whether the image shows only the relevant part or the full page.
            if (pixmap.width() > 1.5*width()) {
                // Assume that the pixmap shows notes and presentation.
                if (pagePart == LeftHalf)
                    pixmap = pixmap.copy(0, 0, pixmap.width()/2, pixmap.height());
                else
                    pixmap = pixmap.copy(pixmap.width()/2, 0, pixmap.width()/2, pixmap.height());
            }
        }
    }
    return pixmap;
}

QByteArray const* PreviewSlide::getCachedBytes(int const index) const
{
    if (cache.contains(index))
        return cache[index];
    else
        return new QByteArray();
}

qint64 PreviewSlide::getCacheSize() const
{
    // Return the total size of all cached images of this label in bytes.
    qint64 size=0;
    for (QMap<int,QByteArray const*>::const_iterator it=cache.cbegin(); it!=cache.cend(); it++) {
        size += qint64((*it)->size());
    }
    return size;
}

void PreviewSlide::clearCache()
{
    // Remove all images from cache.
    qDeleteAll(cache);
    cache.clear();
}

qint64 PreviewSlide::clearCachePage(const int index)
{
    // Delete the given page (page number index+1) from cache and return its size.
    // Return 0 if the page does not exist in cache.
    if (cache.contains(index)) {
        qint64 size = qint64(cache[index]->size());
        delete cache[index];
        cache.remove(index);
        return size;
    }
    else
        return 0;
}
*/

void PreviewSlide::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        for (int i=0; i<links.size(); i++) {
            if (linkPositions[i].contains(event->pos())) {
                switch ( links[i]->linkType() )
                {
                    case Poppler::Link::Goto:
                        if (static_cast<Poppler::LinkGoto*>(links[i])->isExternal()) {
                            // Link to an other document
                            QString filename = static_cast<Poppler::LinkGoto*>(links[i])->fileName();
                            QDesktopServices::openUrl(QUrl(filename, QUrl::TolerantMode));
                        }
                        else {
                            // Link to an other page
                            emit sendNewPageNumber( static_cast<Poppler::LinkGoto*>(links[i])->destination().pageNumber() - 1 );
                        }
                        return;
                    case Poppler::Link::Execute:
                        // Handle execution links, which are marked for execution as an embedded application.
                        // In this case, a corresponding item has been added to embeddedWidgets in renderPage.
                        {
                            Poppler::LinkExecute* link = static_cast<Poppler::LinkExecute*>(links[i]);
                            QStringList splitFileName = QStringList();
                            if (!urlSplitCharacter.isEmpty())
                                splitFileName = link->fileName().split(urlSplitCharacter);
                            else
                                splitFileName.append(link->fileName());
                            QUrl url = QUrl(splitFileName[0], QUrl::TolerantMode);
                            // TODO: handle arguments
                            QDesktopServices::openUrl(url);
                        }
                        break;
                    case Poppler::Link::Browse:
                        // Link to file or website
                        QDesktopServices::openUrl( QUrl(static_cast<Poppler::LinkBrowse*>(links[i])->url(), QUrl::TolerantMode) );
                        break;
                    case Poppler::Link::Action:
                        {
                            Poppler::LinkAction* link = static_cast<Poppler::LinkAction*>(links[i]);
                            switch (link->actionType())
                            {
                                case Poppler::LinkAction::Quit:
                                case Poppler::LinkAction::Close:
                                    emit sendCloseSignal();
                                    return;
                                case Poppler::LinkAction::Print:
                                    qInfo() << "Unsupported link action: print.";
                                    break;
                                case Poppler::LinkAction::GoToPage:
                                    emit focusPageNumberEdit();
                                    break;
                                case Poppler::LinkAction::PageNext:
                                    emit sendNewPageNumber(pageIndex + 1);
                                    return;
                                case Poppler::LinkAction::PagePrev:
                                    emit sendNewPageNumber(pageIndex - 1);
                                    return;
                                case Poppler::LinkAction::PageFirst:
                                    emit sendNewPageNumber(0);
                                    return;
                                case Poppler::LinkAction::PageLast:
                                    emit sendNewPageNumber(-1);
                                    return;
                                case Poppler::LinkAction::Find:
                                    // TODO: implement this
                                    qInfo() << "Unsupported link action: find.";
                                    break;
                                case Poppler::LinkAction::Presentation:
                                    // untested
                                    emit sendShowFullscreen();
                                    break;
                                case Poppler::LinkAction::EndPresentation:
                                    // untested
                                    emit sendEndFullscreen();
                                    break;
                                case Poppler::LinkAction::HistoryBack:
                                    // TODO: implement this
                                    qInfo() << "Unsupported link action: history back.";
                                    break;
                                case Poppler::LinkAction::HistoryForward:
                                    // TODO: implement this
                                    qInfo() << "Unsupported link action: history forward.";
                                    break;
                            }
                        }
                        break;
                    case Poppler::Link::Sound:
                    case Poppler::Link::Movie:
                        qInfo() << "Playing multimedia is not supported in this widget.";
                        break;
                    /*
                    case Poppler::Link::Rendition:
                        qInfo() << "Unsupported link of type rendition";
                        break;
                    case Poppler::Link::JavaScript:
                        qInfo() << "Unsupported link of type JavaScript";
                        break;
                    case Poppler::Link::OCGState: // requires popper >= 0.50
                        qInfo() << "Unsupported link of type OCGState";
                        break;
                    case Poppler::Link::Hide: // requires poppler >= 0.64
                        qInfo() << "Unsupported link of type hide";
                        break;
                    case Poppler::Link::None:
                        qInfo() << "Unsupported link of type none";
                        break;
                    */
                    default:
                        qInfo() << "Unsupported link type" << links[i]->linkType();
                }
            }
        }
    }
    event->accept();
}

void PreviewSlide::mouseMoveEvent(QMouseEvent* event)
{
    // Show the cursor as Qt::PointingHandCursor when hoovering links
    bool is_arrow_pointer = cursor().shape() == Qt::ArrowCursor;
    for (QList<QRect>::const_iterator pos_it=linkPositions.cbegin(); pos_it!=linkPositions.cend(); pos_it++) {
        if (pos_it->contains(event->pos())) {
            if (is_arrow_pointer)
                setCursor(Qt::PointingHandCursor);
            return;
        }
    }
    if (!is_arrow_pointer)
        setCursor(Qt::ArrowCursor);
    event->accept();
}

/*
/// Return cache and clear own cache (without deleting it!).
QMap<int, QByteArray const*> PreviewSlide::ejectCache()
{
    QMap<int, QByteArray const*> newCache = cache;
    cache.clear();
    return newCache;
}

/// Append the given slides to cache, overwriting existing cached images without any checks.
void PreviewSlide::addToCache(QMap<int, QByteArray const*> newCache)
{
    if (cache.isEmpty())
        cache = newCache;
    else {
        for (QMap<int, QByteArray const*>::const_iterator it=newCache.cbegin(); it!=newCache.cend(); it++)
            cache[it.key()] = *it;
    }
}
*/

void PreviewSlide::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.drawPixmap(shiftx, shifty, pixmap);
}

void PreviewSlide::clearAll()
{
    if (cache != nullptr)
        cache->clearCache();
    qDeleteAll(links);
    links.clear();
    linkPositions.clear();
    page = nullptr;
}

int PreviewSlide::getCacheNumber() const
{
    if (cache == nullptr)
        return 0;
    return cache->length();
}

qint64 PreviewSlide::getCacheSize() const
{
    if (cache == nullptr)
        return 0;
    return cache->getSizeBytes();
}

QPixmap const PreviewSlide::getPixmap(int const page)
{
    if (cache == nullptr)
        return QPixmap();
    return cache->renderPixmap(page);
}

/*
void PreviewSlide::clearCache()
{
    if (cache != nullptr)
        cache->clearCache();
}

void PreviewSlide::updateCache(int const page)
{
    if (cache->contains(page))
        return;
    cacheThread->setPage(page);
    cacheThread->start();
}
*/
