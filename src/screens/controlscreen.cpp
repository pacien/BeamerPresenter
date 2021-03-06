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

#include "controlscreen.h"
#include "../names.h"

#ifdef DISABLE_TOOL_TIP
#else
static const QString preview0_tooltip = "Preview of the current presentation slide";
static const QString preview1_tooltip = "Preview of the next presentation slide";
static const QString preview2_tooltip = "Preview of the next presentation slide but one";
static const QString slider_tooltip = "Position of multimedia content on the presentation window";
#endif

// TODO: tidy up! reorganize signals, slots, events, ...

ControlScreen::ControlScreen(QString presentationPath, QString notesPath, PagePart const page, QWidget* parent) :
    QMainWindow(parent),
    ui(new Ui::ControlScreen),
    pagePart(page)
{
    // Check if files are valid.
    {
        setAttribute(Qt::WA_AlwaysShowToolTips);

        // Check whether presentation is empty.
        if (presentationPath.isEmpty()) {
            qCritical() << "No presentation file specified";
            close();
            deleteLater();
            exit(1);
        }
        // Check whether presentation exists.
        QFileInfo checkPresentation(presentationPath);
        if (!checkPresentation.exists() || (!checkPresentation.isFile() && !checkPresentation.isSymLink()) ) {
            qCritical() << "Not a file: " << presentationPath;
            close();
            deleteLater();
            exit(1);
        }
        // Check whether notes are given in a separate file.
        if (notesPath == presentationPath)
            notesPath = "";
        else if (!notesPath.isEmpty()) {
            // Check whether the notes file exists.
            QFileInfo checkNotes(notesPath);
            if (!checkNotes.exists() || (!checkNotes.isFile() && !checkNotes.isSymLink()) ) {
                qCritical() << "Ignoring invalid notes files: " << notesPath;
                notesPath = "";
            }
        }
    }
    // Check whether pagePart is compatible with notesPath.
    if (notesPath != "" && pagePart != FullPage) {
        qCritical() << "Provided additional notes file, but page-part is not full page. Ignoring option for page-part.";
        pagePart = FullPage;
    }

    // Create the presentation pdf document.
    presentation = new PdfDoc(presentationPath);
    // Load the document and check whether it was loaded successfully.
    if (!presentation->loadDocument()) {
        qCritical() << "Could not open document: " << presentationPath;
        close();
        deleteLater();
        exit(1);
    }
    // Save the total number of pages.
    numberOfPages = presentation->getDoc()->numPages();

    // Some numbers for cache management.
    // Maximum number of cached pages is by default the total number of pages.
    maxCacheNumber = numberOfPages;
    // last_delete points to the last page which has not been deleted yet.
    // No pages have been deleted from cache yet.
    last_delete = numberOfPages - 1;

    // Set up presentation screen.
    // The presentation screen is shown immediately.
    presentationScreen = new PresentationScreen(presentation, pagePart);
    // Set the window title.
    presentationScreen->setWindowTitle("BeamerPresenter: " + presentationPath);
#ifdef Q_OS_UNIX
    setWindowIcon(QIcon(ICON_PATH "beamerpresenter.svg"));
#endif

    // Create the UI for the control screen.
    ui->setupUi(this);

    // Load the notes pdf document if a separate notes file is given.
    if (!notesPath.isEmpty()) {
        // Create the notes document.
        notes = new PdfDoc(notesPath);
        // Load the document and check whether it was loaded successfully.
        if (!notes->loadDocument()) {
            qCritical() << "File could not be opened as PDF: " << notesPath;
            // Notes path is reset to "" and no notes file is loaded.
            notesPath = "";
            delete notes;
        }
    }

    // Set the window title.
    if (notesPath.isEmpty()) {
        setWindowTitle("BeamerPresenter: " + presentationPath);
        // The files are equal.
        notes = presentation;
    }
    else
        setWindowTitle("BeamerPresenter: " + notesPath);

    // Set up the slide widgets.
    if (notesPath.isEmpty() && pagePart == FullPage) {
        // No notes are given.
        // Set up the notes widget as a draw slide.
        // The old notes_widget (type MediaSlide) is not required anymore. It will be replaced by a DrawSlide object.
        delete ui->notes_widget;
        // Create the draw slide.
        ui->notes_widget = new DrawSlide(presentation, FullPage, this);
        // ui->notes_widget can get focus.
        ui->notes_widget->setFocusPolicy(Qt::ClickFocus);
        // drawSlide equals notes_widget.
        drawSlide = static_cast<DrawSlide*>(ui->notes_widget);

        // Connect drawSlide to other widgets.
        // Copy paths from draw slide to presentation slide and vice versa when drawing on one of the slides.
        // Copy paths after moving the mouse while drawing. This assumes that only the last path is changed or a new path is created.
        connect(drawSlide->getPathOverlay(), &PathOverlay::pathsChangedQuick, presentationScreen->slide->getPathOverlay(), &PathOverlay::setPathsQuick);
        connect(presentationScreen->slide->getPathOverlay(), &PathOverlay::pathsChangedQuick, drawSlide->getPathOverlay(), &PathOverlay::setPathsQuick);
        // Copy all paths. This completely updates all paths after using the erasor.
        connect(drawSlide->getPathOverlay(), &PathOverlay::pathsChanged, presentationScreen->slide->getPathOverlay(), &PathOverlay::setPaths);
        connect(presentationScreen->slide->getPathOverlay(), &PathOverlay::pathsChanged, drawSlide->getPathOverlay(), &PathOverlay::setPaths);
        // Send pointer position (when using a pointer, torch or magnifier tool).
        connect(drawSlide->getPathOverlay(), &PathOverlay::pointerPositionChanged, presentationScreen->slide->getPathOverlay(), &PathOverlay::setPointerPosition);
        connect(presentationScreen->slide->getPathOverlay(), &PathOverlay::pointerPositionChanged, drawSlide->getPathOverlay(), &PathOverlay::setPointerPosition);
        connect(drawSlide->getPathOverlay(), &PathOverlay::stylusPositionChanged, presentationScreen->slide->getPathOverlay(), &PathOverlay::setStylusPosition);
        connect(presentationScreen->slide->getPathOverlay(), &PathOverlay::stylusPositionChanged, drawSlide->getPathOverlay(), &PathOverlay::setStylusPosition);
        // Send relax signal when the mouse is released, which ends drawing a path.
        connect(drawSlide->getPathOverlay(), &PathOverlay::sendRelax, presentationScreen->slide->getPathOverlay(), &PathOverlay::relax);
        connect(presentationScreen->slide->getPathOverlay(), &PathOverlay::sendRelax, drawSlide->getPathOverlay(), &PathOverlay::relax);
        // Request rendering an enlarged page as required for the magnifier.
        connect(drawSlide->getPathOverlay(), &PathOverlay::sendUpdateEnlargedPage, presentationScreen->slide->getPathOverlay(), &PathOverlay::updateEnlargedPage);
        connect(presentationScreen->slide->getPathOverlay(), &PathOverlay::sendUpdateEnlargedPage, drawSlide->getPathOverlay(), &PathOverlay::updateEnlargedPage);
        // Paths are drawn on a transparent QPixmap for faster rendering.
        // Signals used to request updates for this QPixmap:
        connect(drawSlide->getPathOverlay(), &PathOverlay::sendUpdatePathCache, presentationScreen->slide->getPathOverlay(), &PathOverlay::updatePathCache);
        connect(presentationScreen->slide->getPathOverlay(), &PathOverlay::sendUpdatePathCache, drawSlide->getPathOverlay(), &PathOverlay::updatePathCache);

        // drawSlide should be muted, because it shows the same video content as the presentation slide.
        drawSlide->setMuted(true);

#ifdef DISABLE_TOOL_TIP
#else
        ui->current_slide->setToolTip(preview1_tooltip);
        ui->next_slide->setToolTip(preview2_tooltip);
#endif
    }
    else {
        ui->notes_widget->setDoc(notes, static_cast<PagePart>(-pagePart));
        ui->notes_widget->overwriteCacheMap(new CacheMap(notes, static_cast<PagePart>(-pagePart), this));

#ifdef DISABLE_TOOL_TIP
#else
        ui->current_slide->setToolTip(preview0_tooltip);
        ui->next_slide->setToolTip(preview1_tooltip);
#endif
    }

    // Send pdf documents to slide widgets on control screen.
    ui->current_slide->setDoc(presentation, pagePart);
    ui->next_slide->setDoc(presentation, pagePart);

    // Set common cache for preview slides.
    previewCache = new CacheMap(presentation, pagePart, this);
    ui->current_slide->overwriteCacheMap(previewCache);
    ui->next_slide->overwriteCacheMap(previewCache);

    // Connect cache maps.
    connect(previewCache, &CacheMap::cacheSizeChanged, this, &ControlScreen::updateCacheSize);
    connect(previewCache, &CacheMap::cacheThreadFinished, this, &ControlScreen::cacheThreadFinished);
    connect(ui->notes_widget->getCacheMap(), &CacheMap::cacheSizeChanged, this, &ControlScreen::updateCacheSize);
    connect(ui->notes_widget->getCacheMap(), &CacheMap::cacheThreadFinished, this, &ControlScreen::cacheThreadFinished);
    connect(presentationScreen->slide->getCacheMap(), &CacheMap::cacheSizeChanged, this, &ControlScreen::updateCacheSize);
    connect(presentationScreen->slide->getCacheMap(), &CacheMap::cacheThreadFinished, this, &ControlScreen::cacheThreadFinished);

    // Create widget showing table of content (TocBox) on the control screen.
    // tocBox is empty by default and will be updated when it is shown for the first time.
    tocBox = new TocBox(this);
    // tocBox will be shown on top of the notes widget and should therefore have the same geometry.
    tocBox->setGeometry(ui->notes_widget->geometry());
    // By default tocBox is hidden.
    tocBox->hide();
    // Set the pdf document in the TOC box.
    tocBox->setPdf(presentation);

    // Create widget showing thumbnail slides on the control screen.
    // overviewBox is empty by default and will be updated when it is shown for the first time.
    overviewBox = new OverviewBox(this);
    // overviewBox will be shown on top of the notes widget and should therefore have the same geometry.
    overviewBox->setGeometry(ui->notes_widget->geometry());
    // By default overviewBox is hidden.
    overviewBox->hide();

    // Set up other widgets, which have been created by ui.
    // Display number of pages.
    ui->text_number_slides->setText(QString::number(numberOfPages));
    // Set number of pages (upper bound) for editable text showing current slide number.
    ui->text_current_slide->setNumberOfPages(numberOfPages);
    // Focus on the notes slide widget.
    ui->notes_widget->setFocus();

    // Set up tool selector.
    // Tool selector can send new draw tools to the presentation slide.
    connect(ui->tool_selector, &ToolSelector::sendNewTool, this, &ControlScreen::distributeTools);
    connect(ui->tool_selector, &ToolSelector::sendNewStylusTool, this, &ControlScreen::distributeStylusTools);
    // Tool selector can send KeyActions to control screen.
    connect(ui->tool_selector, &ToolSelector::sendAction, this, &ControlScreen::handleKeyAction);


    // Page requests from the labels:
    // These are emitted if links are clicked.
    // These events are send to control screen and presentation screen.
    connect(ui->notes_widget,  &PreviewSlide::sendNewPageNumber, this, &ControlScreen::receiveNewPageNumber);
    connect(ui->current_slide, &PreviewSlide::sendNewPageNumber, this, &ControlScreen::receiveNewPageNumber);
    connect(ui->next_slide,    &PreviewSlide::sendNewPageNumber, this, &ControlScreen::receiveNewPageNumber);
    connect(ui->notes_widget,  &PreviewSlide::sendNewPageNumber, presentationScreen, &PresentationScreen::receiveNewPage);
    connect(ui->current_slide, &PreviewSlide::sendNewPageNumber, presentationScreen, &PresentationScreen::receiveNewPage);
    connect(ui->next_slide,    &PreviewSlide::sendNewPageNumber, presentationScreen, &PresentationScreen::receiveNewPage);

    // For action links of type "go to page" (interpreted as change page number)
    connect(ui->notes_widget,  &PreviewSlide::focusPageNumberEdit, this, &ControlScreen::focusPageNumberEdit);
    connect(ui->current_slide, &PreviewSlide::focusPageNumberEdit, this, &ControlScreen::focusPageNumberEdit);
    connect(ui->next_slide,    &PreviewSlide::focusPageNumberEdit, this, &ControlScreen::focusPageNumberEdit);
    connect(presentationScreen->slide, &PreviewSlide::focusPageNumberEdit, this, &ControlScreen::focusPageNumberEdit);

    // For action links of type "presentation" (interpreted as full screen)
    // TODO: correct handling full screen.
    //connect(ui->notes_widget,  &PreviewSlide::sendShowFullscreen, this, &ControlScreen::showFullScreen);
    //connect(ui->current_slide, &PreviewSlide::sendShowFullscreen, this, &ControlScreen::showFullScreen);
    //connect(ui->next_slide,    &PreviewSlide::sendShowFullscreen, this, &ControlScreen::showFullScreen);
    //connect(presentationScreen->slide, &PreviewSlide::sendShowFullscreen, this, &ControlScreen::showFullScreen);
    connect(ui->notes_widget,  &PreviewSlide::sendShowFullscreen, presentationScreen, &PresentationScreen::showFullScreen);
    connect(ui->current_slide, &PreviewSlide::sendShowFullscreen, presentationScreen, &PresentationScreen::showFullScreen);
    connect(ui->next_slide,    &PreviewSlide::sendShowFullscreen, presentationScreen, &PresentationScreen::showFullScreen);
    connect(presentationScreen->slide, &PreviewSlide::sendShowFullscreen, presentationScreen, &PresentationScreen::showFullScreen);

    // Navigation signals emitted by presentation screen.
    // Show the same page on control screen as on presentation screen.
    connect(presentationScreen->slide, &PresentationSlide::sendAdaptPage,      this, &ControlScreen::adaptPage);
    // Send a new page number from presentation slide (after following a link) to control screen.
    connect(presentationScreen->slide, &PresentationSlide::sendNewPageNumber,  this, &ControlScreen::receiveNewPageNumber);
    // Presentation screen sends new page numbers after scrolling.
    connect(presentationScreen,        &PresentationScreen::sendNewPageNumber, this, &ControlScreen::receiveNewPageNumber);

    // Other signals emitted by presentation screen.
    // All key events are handled by control screen.
    connect(presentationScreen, &PresentationScreen::sendKeyEvent, this, &ControlScreen::keyPressEvent);
    // Close window (emitted after a link of action type "quit" or "close" was clicked).
    connect(presentationScreen, &PresentationScreen::sendCloseSignal, this, &ControlScreen::close);
    // Send request for multimedia sliders from presentation screen to control screen.
    // Then send the multimedia sliders to draw slide, where they are connected to synchronize multimedia content.
    connect(presentationScreen->slide, &MediaSlide::requestMultimediaSliders, this, &ControlScreen::addMultimediaSliders);

    // Signals sent back to presentation screen.
    connect(this, &ControlScreen::sendNewPageNumber, presentationScreen, &PresentationScreen::renderPage);
    connect(presentationScreen->slide, &PresentationSlide::requestUpdateNotes, this, &ControlScreen::renderPage);
    // Close presentation screen when closing control screen.
    connect(this, &ControlScreen::sendCloseSignal, presentationScreen, &PresentationScreen::close);
    // Close window (emitted after a link of action type "quit" or "close" was clicked).
    connect(ui->notes_widget, &PreviewSlide::sendCloseSignal, presentationScreen, &PresentationScreen::close);
    connect(ui->notes_widget, &PreviewSlide::sendCloseSignal, this, &ControlScreen::close);
    connect(presentationScreen->slide, &PreviewSlide::sendCloseSignal, presentationScreen, &PresentationScreen::close);
    connect(presentationScreen->slide, &PreviewSlide::sendCloseSignal, this, &ControlScreen::close);

    // Connect timer label to presentation PDF and editable total duration.
    // The widget ui->edit_timer is completely controled by ui->label_timer.
    ui->label_timer->init(ui->edit_timer, presentation);
    // Notify timer about new page numbers.
    connect(presentationScreen, &PresentationScreen::pageChanged, ui->label_timer, &Timer::setPage);
    // Send alert (time passed is larger than expected total duration).
    connect(ui->label_timer, &Timer::sendAlert,   this, &ControlScreen::receiveTimerAlert);
    // Stop alert.
    connect(ui->label_timer, &Timer::sendNoAlert, this, &ControlScreen::resetTimerAlert);
    // Exit editing timer with escape key.
    connect(ui->label_timer, &Timer::sendEscape,  this, &ControlScreen::resetFocus);

    // Set up clock.
    // Clock should show current time.
    ui->label_clock->setText(QTime::currentTime().toString("hh:mm:ss"));
    // Clock is updated by the same timer as ui->label_timer.
    connect(ui->label_timer->getTimer(), &QTimer::timeout, ui->label_clock, [&](){ui->label_clock->setText(QTime::currentTime().toString("hh:mm:ss"));});

    // Signals emitted by the page number editor.
    // Pressing return in page number editor changes the page in the presentation.
    connect(ui->text_current_slide, &PageNumberEdit::sendPageNumberReturn, presentationScreen, &PresentationScreen::receiveNewPage);
    // Editing the number in page number editor without pressing return changes the notes pages on the control screen.
    connect(ui->text_current_slide, &PageNumberEdit::sendPageNumberEdit,   this, &ControlScreen::receiveNewPageNumber);
    // Using arrow keys in page number editor shifts the notes pages on the control screen.
    // Currently the key bindings in the page number editor cannot be configured.
    connect(ui->text_current_slide, &PageNumberEdit::sendPageShiftEdit,    this, [&](int const shift){renderPage(currentPageNumber+shift);});
    // Using arrow keys "up" and "down" navigate to the previous or next page skipping overlays.
    connect(ui->text_current_slide, &PageNumberEdit::sendNextSlideStart,   this, &ControlScreen::receiveNextSlideStart);
    connect(ui->text_current_slide, &PageNumberEdit::sendPreviousSlideEnd, this, &ControlScreen::receivePreviousSlideEnd);
    // The escape key resets the focus to the notes slide.
    connect(ui->text_current_slide, &PageNumberEdit::sendEscape,           this, &ControlScreen::resetFocus);

    // Set up cache handling.
    // Set slide widgets for cache thread. The widgets are const for the cache thread.
    // The cache timer just makes sure that slides are rendered to cache after the main thread finished all other tasks.
    connect(cacheTimer, &QTimer::timeout, this, &ControlScreen::updateCacheStep);
    // Send rendered pages from cache thread to control screen.
    // Clear presentation cache when presentation screen is resized.
    connect(presentationScreen, &PresentationScreen::presentationResizeEvent, this, &ControlScreen::presentationResized);

    // Signals emitted by the TOC box (table of contents).
    // Send a destination in pdf (e.g. a section).
    connect(tocBox, &TocBox::sendNewPage, presentationScreen, [&](int const pageNumber){presentationScreen->renderPage(pageNumber, false);});
    connect(tocBox, &TocBox::sendNewPage, this, &ControlScreen::receiveNewPageNumber);

    // Signals emitted by the overview box
    // Send new page number to presentation screen and control screen. TODO: check this.
    connect(overviewBox, &OverviewBox::sendPageNumber, presentationScreen, [&](int const pageNumber){presentationScreen->renderPage(pageNumber, false);});
    connect(overviewBox, &OverviewBox::sendPageNumber, this, &ControlScreen::receiveNewPageNumber);
    // Exit overview box.
    connect(overviewBox, &OverviewBox::sendReturn, this, &ControlScreen::showNotes);

#ifdef DISABLE_TOOL_TIP
#else
    ui->text_number_slides->setToolTip("Total number of pages");
#endif
}

ControlScreen::~ControlScreen()
{
    // Hide widgets which are shown above the notes widget.
    showNotes();
    // Delete widgets which would be shown above the notes widget.
    delete tocBox;
    delete overviewBox;

    // Stop cache processes.
    cacheTimer->disconnect();
    interruptCacheProcesses(10000);
    delete cacheTimer;

    // Disconnect draw slide.
    if (drawSlide != nullptr && drawSlide != ui->notes_widget)
        drawSlide->disconnect();
    // Disconnect all (remaining) widgets from all signals.
    ui->notes_widget->disconnect();
    ui->current_slide->disconnect();
    ui->next_slide->disconnect();
    ui->label_timer->disconnect();
    ui->text_current_slide->disconnect();
    presentationScreen->slide->disconnect();
    presentationScreen->disconnect();
    disconnect();

    // Clear cache maps.
    ui->current_slide->overwriteCacheMap(nullptr);
    ui->next_slide->overwriteCacheMap(nullptr);
    if (previewCache != nullptr)
        previewCache->clearCache();
    if (previewCacheX != nullptr)
        previewCacheX->clearCache();
    ui->notes_widget->getCacheMap()->clearCache();
    if (drawSlideCache != nullptr)
        drawSlideCache->clearCache();
    presentationScreen->slide->getCacheMap()->clearCache();

    // Delete notes cache.
    delete ui->notes_widget->getCacheMap();
    ui->notes_widget->overwriteCacheMap(nullptr);
    // Delete notes pdf.
    if (notes != presentation)
        delete notes;
    // Delete keymap.
    delete keymap;

    // Delete preview cache.
    delete previewCacheX;
    delete previewCache;
    // Delete draw slide.
    if (drawSlide != ui->notes_widget)
        delete drawSlide;
    delete drawSlideCache;
    // Delete presentation screen.
    delete presentationScreen;
    // Delete presentation pdf.
    delete presentation;
    // Delete the user interface.
    delete ui;
}

void ControlScreen::recalcLayout(const int pageNumber)
{
#ifdef DEBUG_RENDERING
    qDebug() << "recalc layout" << size() << oldSize << pageNumber;
#endif
    if (size() != oldSize) {
        // Delete preview cache
        previewCache->clearCache();
        if (previewCacheX != nullptr)
            previewCacheX->clearCache();
        if (drawSlideCache != nullptr)
            drawSlideCache->clearCache();
    }

    // Calculate the size of the side bar.
    /// Aspect ratio (height/width) of the window.
    qreal screenRatio = qreal(height()) / width();
    /// Size of notes page (or presentation slide if drawSlide is shown).
    QSizeF notesSize;
    if (drawSlide == nullptr)
        notesSize = notes->getPageSize(pageNumber);
    else
        notesSize = presentation->getPageSize(pageNumber);
    /// Aspect ratio (height/width) of the slide shown on the notes widget.
    qreal notesSizeRatio = notesSize.height() / notesSize.width();
    // Correct the aspect ratio if the pdf includes slides and notes.
    if (pagePart != FullPage)
        notesSizeRatio *= 2;
    /// Relative width of the notes slide on the control screen.
    /// If the page size is scaled such that screen height == notes height, then relativeNoteWidth = screen width / notes width
    qreal relativeNotesWidth = screenRatio / notesSizeRatio;
    // Make sure that width of notes does not become too large.
    if (relativeNotesWidth > maxNotesWidth)
        relativeNotesWidth = maxNotesWidth;
    // width of the sidebar:
    int sideWidth = int((1-relativeNotesWidth)*width());

    // Adapt widths of different widgets to the new sidebar width.
    // Adapt top level grid layout (notes widget and sidebar).
    ui->gridLayout->setColumnStretch(0, width()-sideWidth);
    ui->gridLayout->setColumnStretch(1, sideWidth);
    // Adapt notes widget geometry.
    ui->notes_widget->setGeometry(0, 0, width()-sideWidth, height());
    // Adapt preview slides sizes.
    ui->current_slide->setMaximumSize(sideWidth, height()/2);
    ui->next_slide->setMaximumSize(sideWidth, height()/2);
    // Width of tool selector.
    ui->tool_selector->setMaximumWidth(sideWidth);
    // Width of timer labels. The colored box is expanding from its minimum width sideWidth/2.
    ui->label_timer->setMinimumWidth(sideWidth/2);
    // Width of total time label. The editable box is expanding to its maximum width sideWidth/3.
    ui->edit_timer->setMaximumWidth(sideWidth/3);

    // Geometry of overview widget: same as of notes widgets.
    overviewBox->setGeometry(0, 0, width()-sideWidth, height());
    // Geometry of TOC widget: same as of notes widgets, but with extra margins in horizontal direction.
    tocBox->setGeometry(int(0.1*(width()-sideWidth)), 0, int(0.8*(width()-sideWidth)), height());

    // Adapt size of draw slide if necessary.
    if (drawSlide != nullptr) {
        /// Scale of draw slide relative to presentation slide.
        qreal scale = drawSlide->getResolution() / presentationScreen->slide->getResolution();
        if (scale < 1e-5)
            scale = 1.;
        // Set scaled tool sizes for draw slide.
        drawSlide->getPathOverlay()->setTool(presentationScreen->slide->getPathOverlay()->getTool(), presentationScreen->slide->getResolution());
        drawSlide->getPathOverlay()->setEraserSize(scale*presentationScreen->slide->getPathOverlay()->getEraserSize());
        if (drawSlide != ui->notes_widget) {
            // Adapt geometry of draw slide: It should have the same geometry as the notes slide.
            drawSlide->setGeometry(ui->notes_widget->rect());
        }
    }

    // Adjust font sizes.
    // The font size is adapted to sideWidth.
    // But if height() is small compared to sideWidth, one should better adapt it to height().
    // This is solved by adapting sideWidth to min(sideWidth, 0.4*height()).
    // From here on sideWidth is not anymore the real width of the sidebar!
    if (5*sideWidth > 2*height())
        sideWidth = 2*height() / 5;
    // Get the current font. Up to the font size it is the same for all labels containing text.
    QFont font = ui->label_timer->font();
    // Smaller font: used for editable total time and "/" between timer and editable total time.
    font.setPixelSize(sideWidth/10+5);
    ui->text_slash_2->setFont(font);
    ui->edit_timer->setFont(font);
    // Larger font: used for all other labels.
    font.setPixelSize(sideWidth/8+7);
    ui->label_timer->setFont(font);
    ui->label_clock->setFont(font);
    ui->text_slash->setFont(font);
    ui->text_current_slide->setFont(font);
    ui->text_number_slides->setFont(font);

    // Adjust the layout containing preview slides and tool selector.
    // Without this the size of the preview slides would not be updated directly.
    ui->gridLayout->activate();
    ui->overviewLayout->activate();
    // Make sure that the sizes of both preview slides are exactly the same.
    if (ui->current_slide->size() != ui->next_slide->size()) {
        QSize minsize = ui->current_slide->size().boundedTo(ui->next_slide->size());
        ui->current_slide->setMaximumSize(minsize);
        ui->next_slide->setMaximumSize(minsize);
    }
    // Notify layout system that geometry has changed.
    updateGeometry();
}

void ControlScreen::focusPageNumberEdit()
{
    this->activateWindow();
    ui->text_current_slide->setFocus();
}

void ControlScreen::addMultimediaSliders(int const n)
{
    // Add n sliders at the bottom of the sidebar
    QList<QSlider*> sliderList = QList<QSlider*>();
    for (int i=0; i<n; i++) {
        QSlider* slider = new QSlider(Qt::Horizontal, this);
        ui->overviewLayout->addWidget(slider);
        sliderList.append(slider);
#ifdef DISABLE_TOOL_TIP
#else
        slider->setToolTip(slider_tooltip);
#endif
    }
    // Send the sliders to the presentation label, where they will be connected to multimedia objects.
    // The presentation label takes ownership of the sliders and will delete them when going to the next slide.
    presentationScreen->slide->setMultimediaSliders(sliderList);
}

void ControlScreen::resetFocus()
{
    // Make sure that the control screen shows the same page as the presentation screen and set focus to the notes label.
    if (currentPageNumber != presentationScreen->getPageNumber())
        renderPage(presentationScreen->getPageNumber());
    ui->notes_widget->setFocus();
}

void ControlScreen::receiveTimerAlert()
{
    QPalette palette = QPalette();
    palette.setColor(QPalette::Window, Qt::red);
    palette.setColor(QPalette::Base, Qt::red);
    ui->edit_timer->setPalette(palette);
}

void ControlScreen::resetTimerAlert()
{
    QPalette palette = QPalette();
    palette.setColor(QPalette::Window, Qt::darkGray);
    palette.setColor(QPalette::Base, Qt::white);
    ui->edit_timer->setPalette(palette);
}

void ControlScreen::renderPage(int const pageNumber, bool const full)
{
    // Update all slide widgets on the control screen to show the given page.
#ifdef DEBUG_RENDERING
    qDebug() << "Render page" << pageNumber << full;
#endif

    // Update currentPageNumber.
    // Negative page numbers are interpreted as signal for going to the last page.
    if (pageNumber < 0 || pageNumber >= numberOfPages)
        currentPageNumber = numberOfPages - 1;
    else
        currentPageNumber = pageNumber;

    // Quick version if this window is not visible. This does not update the slide widgets.
    if (!isVisible()) {
        if (full) {
            // Some extras which may take some time
            if (presentationScreen->slide->getPathOverlay()->getTool().tool == Magnifier)
                presentationScreen->slide->getPathOverlay()->updateEnlargedPage();
            presentationScreen->slide->updateCacheVideos(presentationScreen->pageIndex+1);
        }
        return;
    }

    // Recalculate layout if the window size has changed.
    if (full && size() != oldSize) {
        recalcLayout(currentPageNumber);
        oldSize = size();
    }

    if (drawSlide == nullptr) {
        // Update notes.
        ui->notes_widget->renderPage(currentPageNumber, false);

        // Update current and next slide previews.
        // If we have not reached the last page (there exists a next page):
        ui->current_slide->renderPage(currentPageNumber);
        ui->next_slide->renderPage(currentPageNumber+1);
    }
    else {
        // TODO: improve this part.
        // It is possible that presentationScreen->slide contains drawings which have not been copied to drawSlide yet.
        QString label = presentation->getLabel(currentPageNumber);
        if (drawSlide->getPage() != nullptr && !drawSlide->getPathOverlay()->getPaths().contains(label)) {
            qint16 const sx=presentationScreen->slide->getXshift(), sy=presentationScreen->slide->getYshift();
            qreal res = presentationScreen->slide->getResolution();
            drawSlide->getPathOverlay()->setPaths(label, presentationScreen->slide->getPathOverlay()->getPaths()[label], sx, sy, res);
        }

        // Update current slide
        drawSlide->renderPage(currentPageNumber, false);

        // Synchronize videos
        connectVideos(drawSlide, presentationScreen->slide);

        // Update next slide previews
        // If we have not reached the last page (there exists a next page):
        ui->current_slide->renderPage(currentPageNumber+1);
        ui->next_slide->renderPage(currentPageNumber+2);
    }
    // Update the page number
    ui->text_current_slide->setText(QString::number(currentPageNumber+1));
    if (full) {
        // Some extras which may take some time
        if (presentationScreen->slide->getPathOverlay()->getTool().tool == Magnifier) {
            ui->current_slide->repaint();
            ui->next_slide->repaint();
            presentationScreen->slide->getPathOverlay()->updateEnlargedPage();
            if (drawSlide != nullptr)
                drawSlide->getPathOverlay()->updateEnlargedPage();
        }
        presentationScreen->slide->updateCacheVideos(presentationScreen->pageIndex+1);
    }
}

void ControlScreen::updateCache()
{
    // (Re)start updating cache.
    // This (re)initializes the variables for cache management and starts the cacheTimer, which manages cache updates in a separate thread.

    if (maxCacheSize == 0 || maxCacheNumber == 0) {
        // The cache managment cannot be turned of.
        // If cache is disabled by setting maxCacheSize=0 or maxCacheNumber=0,
        // cache needs to be cleared manually.
        presentationScreen->slide->getCacheMap()->clearCache();
        ui->notes_widget->getCacheMap()->clearCache();
        previewCache->clearCache();
        if (previewCacheX != nullptr)
            previewCacheX->clearCache();
        if (drawSlideCache != nullptr)
            drawSlideCache->clearCache();
        return;
    }

    // Stop running cache updates
    cacheTimer->stop();
    // Number of currently cached slides
    int const cacheNumber = presentationScreen->slide->getCacheMap()->length();
    if (
            cacheNumber == numberOfPages
            && ui->notes_widget->getCacheMap()->length() == numberOfPages
            && previewCache->length() == numberOfPages
            && (drawSlideCache == nullptr || drawSlideCache->length() == numberOfPages)
            && (previewCacheX == nullptr || previewCacheX->length() == numberOfPages)
            ) {
        // All slides are cached
        return;
    }
    // Size of currently cached slides in memory (set to -infinity if it should be ignored)
    if (maxCacheSize > 0) {
        cacheSize = presentationScreen->slide->getCacheMap()->getSizeBytes()
                + ui->notes_widget->getCacheMap()->getSizeBytes()
                + previewCache->getSizeBytes();
        if (previewCacheX != nullptr)
            cacheSize += previewCacheX->getSizeBytes();
        if (drawSlideCache != nullptr)
            cacheSize += drawSlideCache->getSizeBytes();
    }
    else
        // This is approximately -infinity and means that the cache size is unlimited:
        cacheSize = -8589934591L; // -8GiB

    // There should be a simply connected region of cached pages between first_cached and last_cached.
    if (first_cached > currentPageNumber || last_cached < currentPageNumber) {
        // We are outside the simply connected cache region.
        // Reset cache numbers and start a new simply connected region, which is initially empty.
        first_cached = currentPageNumber;
        last_cached = currentPageNumber-1;
        first_delete = 0;
        last_delete = numberOfPages-1;
#ifdef DEBUG_CACHE
        qDebug() << "Reset cache region" << first_delete << first_cached << currentPageNumber << last_cached << last_delete;
#endif
    }
    else {
        // TODO: check whether this is necessary.
        // We are in the simply connected cache region.
        // Make sure that deleting cached pages starts outside this region.
        // Cached pages will only be deleted if the memory or the number of cached pages is limited.
        last_delete = last_delete > currentPageNumber + cacheNumber ? last_delete : currentPageNumber + cacheNumber;
        last_delete = last_delete >= numberOfPages ? numberOfPages-1 : last_delete;
        first_delete = first_delete > currentPageNumber - cacheNumber/2 ? currentPageNumber - cacheNumber/2 : first_delete;
        first_delete = first_delete < 0 ? 0 : first_delete;
    }
    if (last_cached < numberOfPages-1 || first_cached > 0)
        // The simply connected region does not include all pages.
        // Start the update steps by starting the cacheTimer.
        // cacheTimer will call updateCacheStep().
        cacheTimer->start();
}

void ControlScreen::updateCacheStep()
{
    /*
    * Select a page for rendering to cache and tell the CacheThreads of CacheMaps to render that page.
    * Delete cached pages if necessary due to limited memory or a limited number of cached slides.
    * This function will notice when no more pages need to be rendered to cache and stop the cacheTimer.
    *
    * Outline of the cache management:
    *
    * 0. updateCache initializes some variables and starts cacheTimer.
    * 1. cacheTimer calls updateCacheStep in a loop whenever the main thread is not busy.
    * 2. updateCacheStep deletes pages (using freeCachePage) as long as the cache uses
    *    too much memory (or as long as too many slides are cached).
    * 3. updateCacheStep checks whether a new pages should be rendered to cache.
    *    TODO: This process of checking and finding pages should be more understandable.
    *    - If no more cached pages are needed, it stops cacheTimer.
    *    - If it finds a page, which should be rendered to cache:
    *        a. it stops cacheTimer
    *        b. it hands the page to ControlScreen::cachePage.
    * 4. ControlScreen::cachePage calls CacheMap::updateCache for all slide widgets.
    *    This starts CacheThreads which render the different pages in parallel in own threads.
    *    For each new thread the counter ControlScreen::cacheThreadsRunning is incremented.
    * 5. When the rendering is done, CacheMap gets the results from CacheThread and
    *    ControlScreen::cacheThreadFinished is called.
    * 6. cacheThreadFinished decrements cacheThreadsRunning.
    *    If cacheThreadsRunning==0, starts cacheTimer again.
    */

#ifdef DEBUG_CACHE
    qDebug() << "Update cache step" << cacheThreadsRunning << cacheSize << maxCacheSize << maxCacheNumber;
#endif

    // TODO: improve this, make it more deterministic, avoid caching pages which will directly be freed again
    if (
            presentationScreen->slide->getCacheMap()->length() == numberOfPages
            && ui->notes_widget->getCacheMap()->length() == numberOfPages
            && previewCache->length() == numberOfPages
            && (drawSlideCache == nullptr || drawSlideCache->length() == numberOfPages)
            && (previewCacheX == nullptr || previewCacheX->length() == numberOfPages)
            ) {
        // All slides are cached
        qInfo() << "All slides rendered to cache. Cache size:" << cacheSize << "bytes.";
        cacheTimer->stop();
        return;
    }
    if (
            last_cached > last_delete
            || first_cached < first_delete
            || first_cached > currentPageNumber
            || last_cached < currentPageNumber-1
            ) {
        cacheTimer->stop();
#ifdef DEBUG_CACHE
        qDebug() << "Stopped cache timer" << first_delete << first_cached << currentPageNumber << last_cached << last_delete;
#endif
        return;
    }
    // Free space if necessary
    while (cacheSize > maxCacheSize || (maxCacheNumber < numberOfPages && presentationScreen->slide->getCacheMap()->length() > maxCacheNumber) ) {
        // Start deleting later slides if less than 1/4 of the caches slides are previous slides
        if (last_delete > 4*currentPageNumber - 3*first_delete) {
            if (freeCachePage(last_delete))
                break;
            last_delete--;
            last_cached = last_delete > last_cached ? last_cached : last_delete;
        }
        else {
            if (freeCachePage(first_delete))
                break;
            first_delete++;
            first_cached = first_cached > first_delete ? first_cached : first_delete;
        }
        if (last_cached > last_delete || first_cached < first_delete) {
            cacheTimer->stop();
#ifdef DEBUG_CACHE
            qDebug() << "Stopped cache timer: need to reset cache region." << first_delete << first_cached << currentPageNumber << last_cached << last_delete;
#endif
            return;
        }
    }
    if (last_cached+1 == numberOfPages) {
        if (
                first_cached > first_delete
                && 2*maxCacheSize > 3*cacheSize
                && (maxCacheNumber == numberOfPages || 2*maxCacheNumber > 3*presentationScreen->slide->getCacheMap()->length())
                ) {
            // cache first_cached-1
            cachePage(--first_cached);
            return;
        }
        else {
            cacheTimer->stop();
#ifdef DEBUG_CACHE
            qDebug() << "Stopped cache timer" << first_delete << first_cached << currentPageNumber << last_cached << last_delete;
#endif
            return;
        }
    }
    // Don't continue if it is likely that the next cached page would directly be deleted.
    else if (
             // More than 2/3 of available cache space is occupied.
             2*maxCacheSize < 3*cacheSize
             // Enough slides (compared to cache size) after current slide are contained in cache.
             && (last_cached == numberOfPages || 3*(last_cached - currentPageNumber)*cacheSize > 2*presentationScreen->slide->getCacheMap()->length()*maxCacheSize)
             // The remaining cache space is smaller than twice the average space needed per presentation slide.
             && (maxCacheSize - cacheSize)*presentationScreen->slide->getCacheMap()->length() < 2*cacheSize
             ) {
        cacheTimer->stop();
#ifdef DEBUG_CACHE
        qDebug() << "Stopped cache timer" << first_delete << first_cached << currentPageNumber << last_cached << last_delete;
#endif
        return;
    }
    else {
        // Cache the page last_cached+1
        cachePage(++last_cached);
    }
}

bool ControlScreen::freeCachePage(const int page)
{
    if (drawSlideCache != nullptr) {
        cacheSize -= drawSlideCache->clearPage(page);
        if (cacheSize <= maxCacheSize && (maxCacheNumber >= numberOfPages || presentationScreen->slide->getCacheMap()->length() <= maxCacheNumber))
            return true;
    }
    cacheSize -= ui->notes_widget->getCacheMap()->clearPage(page);
    if (cacheSize <= maxCacheSize && (maxCacheNumber >= numberOfPages || presentationScreen->slide->getCacheMap()->length() <= maxCacheNumber))
        return true;
    if (previewCacheX != nullptr)
        cacheSize -= previewCacheX->clearPage(page);
    cacheSize -= previewCache->clearPage(page);
    if (cacheSize <= maxCacheSize && (maxCacheNumber >= numberOfPages || presentationScreen->slide->getCacheMap()->length() <= maxCacheNumber))
        return true;
    cacheSize -= presentationScreen->slide->getCacheMap()->clearPage(page);
#ifdef DEBUG_CACHE
    qDebug() << "Freed page" << page << ". Cache size" << cacheSize << "B";
#endif
    return false;
}

void ControlScreen::cachePage(const int page)
{
#ifdef DEBUG_CACHE
    qDebug() << "Cache page" << page << cacheThreadsRunning << cacheSize;
#endif
    cacheTimer->stop();
    cacheThreadsRunning = 0;
    if (presentationScreen->slide->getCacheMap()->updateCache(page))
        cacheThreadsRunning++;
    if(ui->notes_widget->getCacheMap()->updateCache(page))
        cacheThreadsRunning++;
    if (previewCache->updateCache(page))
        cacheThreadsRunning++;
    if (drawSlideCache != nullptr && drawSlideCache->updateCache(page))
        cacheThreadsRunning++;
    if (previewCacheX != nullptr && previewCacheX->updateCache(page))
        cacheThreadsRunning++;
    if (cacheThreadsRunning == 0)
        cacheTimer->start();
}

void ControlScreen::setCacheNumber(int const number)
{
    if (number < 0)
        maxCacheNumber = numberOfPages;
    else if (number == 0) {
        interruptCacheProcesses(0);
        maxCacheNumber = 0;
    }
    else
        maxCacheNumber = number;
}

void ControlScreen::cacheThreadFinished()
{
    if (--cacheThreadsRunning == 0)
        cacheTimer->start();
}

void ControlScreen::setCacheSize(qint64 const size)
{
    if (cacheSize == 0)
        interruptCacheProcesses(0);
    maxCacheSize = size;
}

void ControlScreen::setTocLevel(quint8 const level)
{
    if (level<1) {
        qWarning() << "toc-depth set to minimum value 1";
        tocBox->setUnfoldLevel(1);
    }
    else if (level>4) {
        qWarning() << "toc-depth set to maximum value 4";
        tocBox->setUnfoldLevel(4);
    }
    else
        tocBox->setUnfoldLevel(level);
}

void ControlScreen::receiveNewPageNumber(int const pageNumber)
{
    renderPage(pageNumber);
    updateCache();
}

void ControlScreen::receiveDest(QString const& dest)
{
    showNotes();
    int const pageNumber = presentation->destToSlide(dest);
    if (pageNumber>=0 && pageNumber<numberOfPages) {
        ui->label_timer->continueTimer();
        emit sendNewPageNumber(pageNumber, true);
        renderPage(pageNumber);
        updateCache();
    }
}

void ControlScreen::receivePreviousSlideEnd()
{
    // Go to the end of the previous real slide (not: previous overlay) on the control screen without changing the presentation screen.
    if (currentPageNumber >= 0) {
        renderPage(notes->getPreviousSlideEnd(currentPageNumber));
        updateCache();
    }
}

void ControlScreen::receiveNextSlideStart()
{
    // Go to the beginning of the next real slide (not: next overlay) on the control screen without changing the presentation screen.
    if (currentPageNumber < numberOfPages - 1) {
        renderPage(notes->getNextSlideIndex(currentPageNumber));
        updateCache();
    }
}

void ControlScreen::adaptPage()
{
    // Synchronize the presentation page to the notes page.
    // This function is called after the page of the presentation is changed.
    ui->label_timer->continueTimer();
    // Go to page shifted relative to the page shown on the presentation screen page.
    if (presentationScreen->slide->getDuration() < 0 || presentationScreen->slide->getDuration() > 0.5) {
        renderPage(presentationScreen->getPageNumber());
        updateCache();
    }
}

void ControlScreen::keyPressEvent(QKeyEvent* event)
{
    // Key codes are given as key + modifiers.
    quint32 const key = quint32(event->key()) + quint32(event->modifiers());
    if (tools.contains(key)) {
        presentationScreen->slide->getPathOverlay()->setTool(tools[key]);
        if (drawSlide != nullptr)
            drawSlide->getPathOverlay()->setTool(tools[key], presentationScreen->slide->getResolution());
#ifdef DEBUG_TOOL_ACTIONS
        qDebug() << "set tool" << tools[key].tool << tools[key].color << tools[key].size << tools[key].extras.magnification;
#endif
    }
    QMap<quint32, QList<KeyAction>>::iterator map_it = keymap->find(key);
    if (map_it == keymap->end())
        return;
    for (QList<KeyAction>::const_iterator action_it=map_it->cbegin(); action_it!=map_it->cend(); action_it++)
        if (handleKeyAction(*action_it))
            break;
    event->accept();
}

bool ControlScreen::handleKeyAction(KeyAction const action)
{
    if (tocBox->isVisible()) {
        switch (action) {
        case KeyAction::Down:
        case KeyAction::Right:
        case KeyAction::Tab:
            // TODO: manually handle key events?
#ifdef DEBUG_KEY_ACTIONS
            qDebug() << "Navigation event in TOC box" << action;
#endif
            return true;
        case KeyAction::Up:
        case KeyAction::Left:
        case KeyAction::ShiftTab:
            // TODO: manually handle key events?
#ifdef DEBUG_KEY_ACTIONS
            qDebug() << "Navigation event in TOC box" << action;
#endif
            return true;
        case KeyAction::Return:
#ifdef DEBUG_KEY_ACTIONS
            qDebug() << "Return event in TOC box" << action;
#endif
            showNotes();
            return true;
        default:
            break;
        }
    }
    else if (overviewBox->isVisible()) {
        switch (action) {
        case KeyAction::Left:
        case KeyAction::PreviousNotes:
        case KeyAction::ShiftTab:
#ifdef DEBUG_KEY_ACTIONS
            qDebug() << "Navigation event in overview box" << action;
#endif
            overviewBox->moveFocusLeft();
            return true;
        case KeyAction::Right:
        case KeyAction::NextNotes:
        case KeyAction::Tab:
#ifdef DEBUG_KEY_ACTIONS
            qDebug() << "Navigation event in overview box" << action;
#endif
            overviewBox->moveFocusRight();
            return true;
        case KeyAction::Down:
#ifdef DEBUG_KEY_ACTIONS
            qDebug() << "Navigation event in overview box" << action;
#endif
            overviewBox->moveFocusDown();
            return true;
        case KeyAction::Up:
#ifdef DEBUG_KEY_ACTIONS
            qDebug() << "Navigation event in overview box" << action;
#endif
            overviewBox->moveFocusUp();
            return true;
        case KeyAction::End:
#ifdef DEBUG_KEY_ACTIONS
            qDebug() << "Navigation event in overview box" << action;
#endif
            overviewBox->setFocused(1073741823);
            return true;
        case KeyAction::First:
#ifdef DEBUG_KEY_ACTIONS
            qDebug() << "Navigation event in overview box" << action;
#endif
            overviewBox->setFocused(0);
            return true;
        case KeyAction::Return:
#ifdef DEBUG_KEY_ACTIONS
            qDebug() << "Return event in overview box" << action;
#endif
            emit sendNewPageNumber(overviewBox->getPage(), true);
            showNotes();
            return true;
        case KeyAction::Next:
#ifdef DEBUG_KEY_ACTIONS
            qDebug() << "Page change event in overview box" << action;
#endif
            currentPageNumber = presentationScreen->getPageNumber() + 1;
            emit sendNewPageNumber(currentPageNumber, true);
            overviewBox->setFocused(currentPageNumber);
            break;
        case KeyAction::Previous:
#ifdef DEBUG_KEY_ACTIONS
            qDebug() << "Page change event in overview box" << action;
#endif
            currentPageNumber = presentationScreen->getPageNumber() - 1;
            if (currentPageNumber >= 0)
                emit sendNewPageNumber(currentPageNumber, false);
            else
                currentPageNumber = 0;
            overviewBox->setFocused(currentPageNumber);
            break;
        case KeyAction::NextSkippingOverlays:
#ifdef DEBUG_KEY_ACTIONS
            qDebug() << "Page change event in overview box" << action;
#endif
            currentPageNumber = presentation->getNextSlideIndex(presentationScreen->getPageNumber());
            emit sendNewPageNumber(currentPageNumber, true);
            overviewBox->setFocused(currentPageNumber);
            break;
        case KeyAction::PreviousSkippingOverlays:
#ifdef DEBUG_KEY_ACTIONS
            qDebug() << "Page change event in overview box" << action;
#endif
            currentPageNumber = presentation->getPreviousSlideEnd(presentationScreen->getPageNumber());
            emit sendNewPageNumber(currentPageNumber, false);
            overviewBox->setFocused(currentPageNumber);
            break;
        case KeyAction::NextNotesSkippingOverlays:
#ifdef DEBUG_KEY_ACTIONS
            qDebug() << "Page change notes event in overview box" << action;
#endif
            currentPageNumber = presentation->getNextSlideIndex(currentPageNumber);
            overviewBox->setFocused(currentPageNumber);
            break;
        case KeyAction::PreviousNotesSkippingOverlays:
#ifdef DEBUG_KEY_ACTIONS
            qDebug() << "Page change notes event in overview box" << action;
#endif
            if (currentPageNumber > 0) {
                currentPageNumber = presentation->getPreviousSlideEnd(currentPageNumber);
                overviewBox->setFocused(currentPageNumber);
            }
            break;
        case KeyAction::PreviousNoTransition:
#ifdef DEBUG_KEY_ACTIONS
            qDebug() << "Page change event in overview box" << action;
#endif
            currentPageNumber = presentationScreen->getPageNumber() - 1;
            presentationScreen->slide->disableTransitions();
            if (currentPageNumber >= 0)
                emit sendNewPageNumber(currentPageNumber, false);
            else
                currentPageNumber = 0;
            presentationScreen->slide->enableTransitions();
            overviewBox->setFocused(currentPageNumber);
            break;
        case KeyAction::NextNoTransition:
#ifdef DEBUG_KEY_ACTIONS
            qDebug() << "Page change event in overview box" << action;
#endif
            currentPageNumber = presentationScreen->getPageNumber() + 1;
            presentationScreen->slide->disableTransitions();
            emit sendNewPageNumber(currentPageNumber, true);
            presentationScreen->slide->enableTransitions();
            overviewBox->setFocused(currentPageNumber);
            break;
        default:
            break;
        }
    }
    // Handle any kind of action sent by a key binding or a button.
    switch (action) {
    case KeyAction::Next:
#ifdef DEBUG_KEY_ACTIONS
            qDebug() << "Page change event" << action;
#endif
        currentPageNumber = presentationScreen->getPageNumber() + 1;
        ui->label_timer->continueTimer();
        emit sendNewPageNumber(currentPageNumber, true);
        if (isVisible())
            showNotes();
        break;
    case KeyAction::Previous:
#ifdef DEBUG_KEY_ACTIONS
            qDebug() << "Page change event" << action;
#endif
        currentPageNumber = presentationScreen->getPageNumber() - 1;
        if (currentPageNumber >= 0) {
            ui->label_timer->continueTimer();
            emit sendNewPageNumber(currentPageNumber, false);
            showNotes();
        }
        else {
            currentPageNumber = 0;
        }
        break;
    case KeyAction::NextNotes:
#ifdef DEBUG_KEY_ACTIONS
            qDebug() << "Page change notes event" << action;
#endif
        if (isVisible()) {
            renderPage(++currentPageNumber);
            showNotes();
        }
        break;
    case KeyAction::PreviousNotes:
#ifdef DEBUG_KEY_ACTIONS
            qDebug() << "Page change notes event" << action;
#endif
        if (isVisible()) {
            if (currentPageNumber > 0)
                renderPage(--currentPageNumber);
            showNotes();
        }
        break;
    case KeyAction::NextSkippingOverlays:
#ifdef DEBUG_KEY_ACTIONS
            qDebug() << "Page change event" << action;
#endif
        currentPageNumber = presentation->getNextSlideIndex(presentationScreen->getPageNumber());
        ui->label_timer->continueTimer();
        emit sendNewPageNumber(currentPageNumber, true);
        if (isVisible())
            showNotes();
        break;
    case KeyAction::PreviousSkippingOverlays:
#ifdef DEBUG_KEY_ACTIONS
            qDebug() << "Page change event" << action;
#endif
        currentPageNumber = presentation->getPreviousSlideEnd(presentationScreen->getPageNumber());
        ui->label_timer->continueTimer();
        emit sendNewPageNumber(currentPageNumber, false);
        if (isVisible())
            showNotes();
        break;
    case KeyAction::NextNotesSkippingOverlays:
#ifdef DEBUG_KEY_ACTIONS
            qDebug() << "Page change event" << action;
#endif
        if (isVisible()) {
            currentPageNumber = presentation->getNextSlideIndex(currentPageNumber);
            renderPage(currentPageNumber);
            showNotes();
        }
        break;
    case KeyAction::PreviousNotesSkippingOverlays:
#ifdef DEBUG_KEY_ACTIONS
            qDebug() << "Page change event" << action;
#endif
        if (isVisible()) {
            if (currentPageNumber > 0) {
                currentPageNumber = presentation->getPreviousSlideEnd(currentPageNumber);
                renderPage(currentPageNumber);
            }
            showNotes();
        }
        break;
    case KeyAction::PreviousNoTransition:
#ifdef DEBUG_KEY_ACTIONS
            qDebug() << "Page change event" << action;
#endif
        currentPageNumber = presentationScreen->getPageNumber() - 1;
        presentationScreen->slide->disableTransitions();
        if (currentPageNumber >= 0) {
            ui->label_timer->continueTimer();
            emit sendNewPageNumber(currentPageNumber, false);
            showNotes();
        }
        else
            currentPageNumber = 0;
        presentationScreen->slide->enableTransitions();
        break;
    case KeyAction::NextNoTransition:
#ifdef DEBUG_KEY_ACTIONS
            qDebug() << "Page change event" << action;
#endif
        currentPageNumber = presentationScreen->getPageNumber() + 1;
        ui->label_timer->continueTimer();
        presentationScreen->slide->disableTransitions();
        emit sendNewPageNumber(currentPageNumber, true);
        presentationScreen->slide->enableTransitions();
        if (isVisible())
            showNotes();
        break;
    case KeyAction::Update:
#ifdef DEBUG_KEY_ACTIONS
            qDebug() << "Update event" << action;
#endif
        currentPageNumber = presentationScreen->getPageNumber();
        ui->label_timer->continueTimer();
        emit sendNewPageNumber(currentPageNumber, true); // TODO: what happens to duration if page is updated?
        if (isVisible())
            showNotes();
        break;
    case KeyAction::LastPage:
#ifdef DEBUG_KEY_ACTIONS
            qDebug() << "Page change event" << action;
#endif
        currentPageNumber = numberOfPages - 1;
        emit sendNewPageNumber(currentPageNumber, false);
        if (isVisible())
            showNotes();
        break;
    case KeyAction::FirstPage:
#ifdef DEBUG_KEY_ACTIONS
            qDebug() << "Page change event" << action;
#endif
        currentPageNumber = 0;
        emit sendNewPageNumber(currentPageNumber, true);
        if (isVisible())
            showNotes();
        break;
    case KeyAction::UpdateCache:
#ifdef DEBUG_KEY_ACTIONS
            qDebug() << "Update cache event" << action;
#endif
        updateCache();
        break;
#ifdef EMBEDDED_APPLICATIONS_ENABLED
    case KeyAction::StartEmbeddedCurrentSlide:
#ifdef DEBUG_KEY_ACTIONS
            qDebug() << "Start embedded event" << action;
#endif
        presentationScreen->slide->startAllEmbeddedApplications(presentationScreen->getPageNumber());
        //ui->notes_widget->startAllEmbeddedApplications(currentPageNumber);
        break;
    case KeyAction::StartAllEmbedded:
#ifdef DEBUG_KEY_ACTIONS
            qDebug() << "Start embedded event" << action;
#endif
        startAllEmbeddedApplications();
        break;
    case KeyAction::CloseEmbeddedCurrentSlide:
    {
#ifdef DEBUG_KEY_ACTIONS
            qDebug() << "Close embedded event" << action;
#endif
        presentationScreen->slide->closeEmbeddedApplications(presentationScreen->getPageNumber());
        ui->notes_widget->closeEmbeddedApplications(presentationScreen->getPageNumber());
    }
        break;
    case KeyAction::CloseAllEmbedded:
#ifdef DEBUG_KEY_ACTIONS
            qDebug() << "Close embedded event" << action;
#endif
        presentationScreen->slide->closeAllEmbeddedApplications();
        ui->notes_widget->closeAllEmbeddedApplications();
        break;
#endif
    case KeyAction::GoToPage:
#ifdef DEBUG_KEY_ACTIONS
            qDebug() << "To to page event" << action;
#endif
        if (isVisible()) {
            showNotes();
            ui->text_current_slide->setFocus();
        }
        break;
    case KeyAction::PlayMultimedia:
#ifdef DEBUG_KEY_ACTIONS
            qDebug() << "Play multimedia event" << action;
#endif
        presentationScreen->slide->startAllMultimedia();
        break;
    case KeyAction::PauseMultimedia:
#ifdef DEBUG_KEY_ACTIONS
            qDebug() << "Pause multimedia event" << action;
#endif
        presentationScreen->slide->pauseAllMultimedia();
        if (drawSlide != nullptr)
            ui->notes_widget->pauseAllMultimedia();
        break;
    case KeyAction::PlayPauseMultimedia:
        {
            bool running = ui->notes_widget->hasActiveMultimediaContent() || presentationScreen->slide->hasActiveMultimediaContent();
            if (running) {
#ifdef DEBUG_KEY_ACTIONS
            qDebug() << "Toggle multimedia event: pause" << action;
#endif
                presentationScreen->slide->pauseAllMultimedia();
                ui->notes_widget->pauseAllMultimedia();
            }
            else {
#ifdef DEBUG_KEY_ACTIONS
            qDebug() << "Toggle multimedia event: play" << action;
#endif
                presentationScreen->slide->startAllMultimedia();
            }
        }
        break;
    case KeyAction::ToggleMuteAll:
    {
        bool mute = !presentationScreen->slide->isMuted();
#ifdef DEBUG_KEY_ACTIONS
        qDebug() << "Toggle mute event: mute=" << mute << action;
#endif
        presentationScreen->slide->setMuted(mute);
        ui->notes_widget->setMuted(mute);
        if (drawSlide != nullptr && drawSlide != ui->notes_widget)
            drawSlide->setMuted(mute);
        break;
    }
    case KeyAction::ToggleMuteNotes:
        ui->notes_widget->setMuted(!ui->notes_widget->isMuted());
#ifdef DEBUG_KEY_ACTIONS
        qDebug() << "Toggle mute notes event: mute=" << ui->notes_widget->isMuted() << action;
#endif
        if (drawSlide != nullptr && drawSlide != ui->notes_widget)
            drawSlide->setMuted(ui->notes_widget->isMuted());
        break;
    case KeyAction::ToggleMutePresentation:
        presentationScreen->slide->setMuted(!presentationScreen->slide->isMuted());
#ifdef DEBUG_KEY_ACTIONS
        qDebug() << "Toggle mute presentation event: mute=" << presentationScreen->slide->isMuted() << action;
#endif
        break;
    case KeyAction::MuteAll:
#ifdef DEBUG_KEY_ACTIONS
        qDebug() << "Mute all event" << action;
#endif
        presentationScreen->slide->setMuted(true);
        ui->notes_widget->setMuted(true);
        if (drawSlide != nullptr && drawSlide != ui->notes_widget)
            drawSlide->setMuted(true);
        break;
    case KeyAction::MuteNotes:
#ifdef DEBUG_KEY_ACTIONS
        qDebug() << "Mute notes event" << action;
#endif
        ui->notes_widget->setMuted(true);
        if (drawSlide != nullptr && drawSlide != ui->notes_widget)
            drawSlide->setMuted(true);
        break;
    case KeyAction::MutePresentation:
#ifdef DEBUG_KEY_ACTIONS
        qDebug() << "Mute presentation event" << action;
#endif
        presentationScreen->slide->setMuted(true);
        break;
    case KeyAction::UnmuteAll:
#ifdef DEBUG_KEY_ACTIONS
        qDebug() << "Unmute all event" << action;
#endif
        presentationScreen->slide->setMuted(false);
        ui->notes_widget->setMuted(false);
        if (drawSlide != nullptr && drawSlide != ui->notes_widget)
            drawSlide->setMuted(false);
        break;
    case KeyAction::UnmuteNotes:
#ifdef DEBUG_KEY_ACTIONS
        qDebug() << "Unmute notes event" << action;
#endif
        ui->notes_widget->setMuted(false);
        if (drawSlide != nullptr && drawSlide != ui->notes_widget)
            drawSlide->setMuted(false);
        break;
    case KeyAction::UnmutePresentation:
#ifdef DEBUG_KEY_ACTIONS
        qDebug() << "Unmute presentation event" << action;
#endif
        presentationScreen->slide->setMuted(false);
        break;
    case KeyAction::ShowCursor:
#ifdef DEBUG_KEY_ACTIONS
        qDebug() << "Show cursor event" << action;
#endif
        presentationScreen->slide->getPathOverlay()->showPointer();
        break;
    case KeyAction::HideCursor:
#ifdef DEBUG_KEY_ACTIONS
        qDebug() << "Hide cursor event" << action;
#endif
        presentationScreen->slide->getPathOverlay()->hidePointer();
        break;
    case KeyAction::ToggleCursor:
#ifdef DEBUG_KEY_ACTIONS
        qDebug() << "Toggle cursor event" << action;
#endif
        presentationScreen->slide->getPathOverlay()->togglePointerVisibility();
        break;
    case KeyAction::PlayPauseTimer:
#ifdef DEBUG_KEY_ACTIONS
        qDebug() << "Toggle timer event" << action;
#endif
        ui->label_timer->toggleTimer();
        break;
    case KeyAction::ContinueTimer:
#ifdef DEBUG_KEY_ACTIONS
        qDebug() << "Continue timer event" << action;
#endif
        ui->label_timer->continueTimer();
        break;
    case KeyAction::PauseTimer:
#ifdef DEBUG_KEY_ACTIONS
        qDebug() << "Pause timer event" << action;
#endif
        ui->label_timer->pauseTimer();
        break;
    case KeyAction::ResetTimer:
#ifdef DEBUG_KEY_ACTIONS
        qDebug() << "Reset timer event" << action;
#endif
        ui->label_timer->resetTimer();
        break;
    case KeyAction::ShowTOC:
#ifdef DEBUG_KEY_ACTIONS
        qDebug() << "Show TOC event" << action;
#endif
        if (isVisible())
            showToc();
        break;
    case KeyAction::ToggleTOC:
        if (tocBox->isVisible()) {
#ifdef DEBUG_KEY_ACTIONS
            qDebug() << "Toggle TOC event: hide TOC" << action;
#endif
            showNotes();
        }
        else if (isVisible()) {
#ifdef DEBUG_KEY_ACTIONS
            qDebug() << "Toggle TOC event: show TOC" << action;
#endif
            showToc();
        }
        break;
    case KeyAction::ShowOverview:
#ifdef DEBUG_KEY_ACTIONS
        qDebug() << "Show overview event" << action;
#endif
        if (isVisible())
            showOverview();
        break;
    case KeyAction::ToggleOverview:
        if (overviewBox->isVisible()) {
#ifdef DEBUG_KEY_ACTIONS
            qDebug() << "Toggle overview event: hide overview" << action;
#endif
            showNotes();
        }
        else if (isVisible()) {
#ifdef DEBUG_KEY_ACTIONS
            qDebug() << "Toggle overview event: show overview" << action;
#endif
            showOverview();
        }
        break;
    case KeyAction::HideDrawSlide:
#ifdef DEBUG_KEY_ACTIONS
        qDebug() << "Hide draw slide event" << action;
#endif
        if (isVisible())
            hideDrawSlide();
        break;
    case KeyAction::HideOverlays:
#ifdef DEBUG_KEY_ACTIONS
        qDebug() << "Hide overlays event" << action;
#endif
        showNotes();
        break;
    case KeyAction::Reload:
#ifdef DEBUG_KEY_ACTIONS
        qDebug() << "Reload files event" << action;
#endif
        reloadFiles();
        break;
    case KeyAction::SyncFromControlScreen:
#ifdef DEBUG_KEY_ACTIONS
        qDebug() << "Sync presentation event" << action;
#endif
        if (isVisible()) {
            ui->label_timer->continueTimer();
            if (presentationScreen->slide->pageNumber() != currentPageNumber)
                emit sendNewPageNumber(currentPageNumber, true); // TODO: make this configurable (true/false)?
            showNotes();
            updateCache();
        }
        break;
    case KeyAction::SyncFromPresentationScreen:
#ifdef DEBUG_KEY_ACTIONS
        qDebug() << "Sync notes event" << action;
#endif
        if (isVisible() && presentationScreen->getPageNumber() != currentPageNumber) {
            currentPageNumber = presentationScreen->getPageNumber();
            renderPage(currentPageNumber);
            updateCache();
        }
        break;
    case KeyAction::FullScreen:
#ifdef DEBUG_KEY_ACTIONS
        qDebug() << "Full screen event" << action;
#endif
        if (presentationScreen->windowState() == Qt::WindowFullScreen)
            presentationScreen->showNormal();
        else
            presentationScreen->showFullScreen();
        break;
    case KeyAction::Quit:
#ifdef DEBUG_KEY_ACTIONS
        qDebug() << "Close event" << action;
#endif
        emit sendCloseSignal();
        close();
        break;
    case KeyAction::ClearAnnotations:
#ifdef DEBUG_KEY_ACTIONS
        qDebug() << "Clear event" << action;
#endif
        presentationScreen->slide->getPathOverlay()->clearPageAnnotations();
        if (drawSlide != nullptr)
            drawSlide->getPathOverlay()->clearPageAnnotations();
        break;
    case KeyAction::DrawNone:
#ifdef DEBUG_KEY_ACTIONS
        qDebug() << "Set tool none event" << action;
#endif
        presentationScreen->slide->getPathOverlay()->setTool(NoTool);
        if (drawSlide != nullptr)
            drawSlide->getPathOverlay()->setTool(NoTool);
        break;
    case KeyAction::DrawEraser:
#ifdef DEBUG_KEY_ACTIONS
        qDebug() << "Set tool eraser event" << action;
#endif
        presentationScreen->slide->getPathOverlay()->setTool(Eraser);
        if (drawSlide != nullptr)
            drawSlide->getPathOverlay()->setTool(Eraser, presentationScreen->slide->getResolution());
        break;
    case KeyAction::DrawMode:
#ifdef DEBUG_KEY_ACTIONS
        qDebug() << "Draw mode event" << action;
#endif
        if (isVisible())
            showDrawSlide();
        break;
    case KeyAction::ToggleDrawMode:
        if (isVisible()) {
            if (drawSlide == nullptr || drawSlide->isHidden()) {
#ifdef DEBUG_KEY_ACTIONS
                qDebug() << "Togge draw mode event: enter draw mode" << action;
#endif
                showDrawSlide();
            }
            else if (drawSlide != ui->notes_widget) {
#ifdef DEBUG_KEY_ACTIONS
                qDebug() << "Togge draw mode event: exit draw mode" << action;
#endif
                hideDrawSlide();
            }
        }
        break;
    case KeyAction::UndoDrawing:
#ifdef DEBUG_KEY_ACTIONS
        qDebug() << "Undo drawing event" << action;
#endif
        presentationScreen->slide->getPathOverlay()->undoPath();
        break;
    case KeyAction::RedoDrawing:
#ifdef DEBUG_KEY_ACTIONS
        qDebug() << "Redo drawing event" << action;
#endif
        presentationScreen->slide->getPathOverlay()->redoPath();
        break;
    case KeyAction::SaveDrawings:
        {
#ifdef DEBUG_KEY_ACTIONS
            qDebug() << "Save drawings event" << action;
#endif
            QString const savePath = QFileDialog::getSaveFileName(this, "Save drawings");
            if (!savePath.isEmpty())
                presentationScreen->slide->getPathOverlay()->saveXML(savePath, notes);
        }
        break;
    case KeyAction::SaveDrawingsXournal:
        {
#ifdef DEBUG_KEY_ACTIONS
            qDebug() << "Save drawings event" << action;
#endif
            QString const savePath = QFileDialog::getSaveFileName(this, "Save drawings compatibility (Xournal)");
            if (!savePath.isEmpty())
                presentationScreen->slide->getPathOverlay()->saveXournal(savePath);
        }
        break;
    case KeyAction::SaveDrawingsLegacy:
        {
#ifdef DEBUG_KEY_ACTIONS
            qDebug() << "Save drawings event" << action;
#endif
            QString const savePath = QFileDialog::getSaveFileName(this, "Save drawings legacy (deprecated!)");
            if (!savePath.isEmpty())
                presentationScreen->slide->getPathOverlay()->saveDrawings(savePath, notes->getPath());
        }
        break;
    case KeyAction::SaveDrawingsUncompressed:
        {
#ifdef DEBUG_KEY_ACTIONS
            qDebug() << "Save drawings event" << action;
#endif
            QString const savePath = QFileDialog::getSaveFileName(this, "Save drawings uncompressed");
            if (!savePath.isEmpty())
                presentationScreen->slide->getPathOverlay()->saveXML(savePath, notes, false);
        }
        break;
    case KeyAction::LoadDrawings:
        {
#ifdef DEBUG_KEY_ACTIONS
            qDebug() << "Load drawings event" << action;
#endif
            QString const loadPath = QFileDialog::getOpenFileName(this, "Load drawings");
            if (!loadPath.isEmpty())
                presentationScreen->slide->getPathOverlay()->loadXML(loadPath, notes);
        }
        break;
    case NoAction:
#ifdef DEBUG_KEY_ACTIONS
        qDebug() << "NoAction event" << action;
#endif
        break;
    default:
        FullDrawTool const tool = defaultToolConfig.value(actionToToolMap.value(action, InvalidTool));
        if (tool.tool != InvalidTool) {
#ifdef DEBUG_TOOL_ACTIONS
        qDebug() << "set tool" << tool.tool << tool.color << tool.size << tool.extras.magnification;
#endif
            presentationScreen->slide->getPathOverlay()->setTool(tool);
            if (drawSlide != nullptr)
                drawSlide->getPathOverlay()->setTool(tool, presentationScreen->slide->getResolution());
        }
        break;
    }
    // Return false if the event was handled normally.
    return false;
}

#ifdef EMBEDDED_APPLICATIONS_ENABLED
void ControlScreen::startAllEmbeddedApplications()
{
    // Start all embedded applications of the presentation on all pages.
#ifdef DEBUG_MULTIMEDIA
    qDebug() << "Starting all embedded applications on all pages.";
#endif
    for (int i=0; i<presentation->getDoc()->numPages(); i++) {
        presentationScreen->slide->initEmbeddedApplications(i);
        presentationScreen->slide->startAllEmbeddedApplications(i);
    }
}
#endif

void ControlScreen::resizeEvent(QResizeEvent* event)
{
    // When the control screen window is resized, the sizes of the page labels change and the cached pages become useless.
    // Stop rendering to cache, delete cached pages for the control screen labels and reset cached region.
    interruptCacheProcesses(0);

    // Update layout
    recalcLayout(currentPageNumber);
    oldSize = event->size();
    first_cached = presentationScreen->getPageNumber();
    last_cached = first_cached-1;
    first_delete = 0;
    last_delete = numberOfPages-1;
    ui->notes_widget->getCacheMap()->clearCache();
    previewCache->clearCache();
    if (previewCacheX != nullptr)
        previewCacheX->clearCache();
    if (drawSlideCache != nullptr)
        drawSlideCache->clearCache();
    overviewBox->setOutdated();
    // Render current page.
    ui->notes_widget->renderPage(ui->notes_widget->pageNumber(), false);
    ui->current_slide->renderPage(ui->current_slide->pageNumber());
    ui->next_slide->renderPage(ui->next_slide->pageNumber());
    if (drawSlide != nullptr && drawSlideCache != nullptr)
        drawSlide->renderPage(presentationScreen->getPageNumber(), false);
}

void ControlScreen::presentationResized()
{
    // Stop rendering to cache and reset cached region.
    cacheTimer->stop();
    first_cached = presentationScreen->getPageNumber();
    last_cached = first_cached-1;
    first_delete = 0;
    last_delete = numberOfPages-1;

    // Adapt tool sizes.
    if (drawSlide != nullptr) {
        drawSlide->getPathOverlay()->setTool(presentationScreen->slide->getPathOverlay()->getTool(), presentationScreen->slide->getResolution());
        /// Scale of draw slide relative to presentation slide.
        qreal scale = drawSlide->getResolution() / presentationScreen->slide->getResolution();
        if (scale < 1e-5)
            scale = 1.;
        drawSlide->getPathOverlay()->setEraserSize(scale*presentationScreen->slide->getPathOverlay()->getEraserSize());
    }
}

void ControlScreen::setColor(const QColor bgColor, const QColor textColor)
{
    // Set background and text color for the control screen.
    QPalette newPalette(palette());
    newPalette.setColor(QPalette::Window, bgColor);
    newPalette.setColor(QPalette::Base, bgColor);
    newPalette.setColor(QPalette::Text, textColor);
    newPalette.setColor(QPalette::WindowText, textColor);
    setPalette(newPalette);
    ui->text_current_slide->setPalette(newPalette);
}

void ControlScreen::setPresentationColor(const QColor color)
{
    // Set background color for the presentation screen.
    QPalette newPalette(presentationScreen->palette());
    newPalette.setColor(QPalette::Window, color);
    newPalette.setColor(QPalette::Base, color);
    presentationScreen->setPalette(newPalette);
}

void ControlScreen::setScrollDelta(const int scrollDelta)
{
    this->scrollDelta = scrollDelta;
    presentationScreen->setScrollDelta(scrollDelta);
}

void ControlScreen::setForceTouchpad()
{
    forceIsTouchpad = true;
    presentationScreen->setForceTouchpad();
}

void ControlScreen::wheelEvent(QWheelEvent* event)
{
    // Handle mouse wheel or touch pad scrolling events.

    // Change the signs in the beginning, this makes the rest less confusing.
    int deltaPix = -event->pixelDelta().y();
    int deltaAngle = -event->angleDelta().y();
    int deltaPages;
    // If a touch pad was used for scrolling:
    if (deltaPix != 0) {
        scrollState += deltaPix;
        deltaPages = scrollState / scrollDelta;
        if (deltaPages<0)
            deltaPages++;
        scrollState -= scrollDelta*deltaPages;
    }
    else if (forceIsTouchpad) {
        scrollState += deltaAngle;
        deltaPages = scrollState / scrollDelta;
        if (deltaPages<0)
            deltaPages++;
        scrollState -= scrollDelta*deltaPages;
    }
    // If a mouse wheel was used for scrolling:
    else {
        if (deltaAngle > 120)
            deltaPages = deltaAngle / 120;
        else if (deltaAngle > 0)
            deltaPages = 1;
        else if (deltaAngle < -120)
            deltaPages = -((-deltaAngle) / 120);
        else if (deltaAngle < 0)
            deltaPages = -1;
        else
            deltaPages = 0;
    }
    if (deltaPages + currentPageNumber < 0) {
        if (currentPageNumber != 0)
            renderPage(0);
    }
    else if (deltaPages != 0) {
        renderPage(currentPageNumber + deltaPages);
        updateCache();
    }
    event->accept();
}

#ifdef EMBEDDED_APPLICATIONS_ENABLED
void ControlScreen::setEmbedFileList(const QStringList &files)
{
    // Set list of files, which should be be executed as embedded widgets if they are linked in the PDF.
    ui->notes_widget->setEmbedFileList(files);
    presentationScreen->slide->setEmbedFileList(files);
}

void ControlScreen::setPid2WidConverter(QString const &program)
{
    // Set an external program for converting process IDs to window IDs.
    QFileInfo fileinfo = QFileInfo(program);
    if (fileinfo.isFile() && fileinfo.isExecutable()) {
        ui->notes_widget->setPid2Wid(program);
        presentationScreen->slide->setPid2Wid(program);
    }
    else
        qCritical() << "Can't use program: not a file or not executable." << program;
}
#endif

void ControlScreen::setUrlSplitCharacter(QString const &splitCharacter)
{
    // Set a character, which splits links to files into file paths and arguments.
    ui->notes_widget->setUrlSplitCharacter(splitCharacter);
    presentationScreen->slide->setUrlSplitCharacter(splitCharacter);
}

void ControlScreen::showToc()
{
    overviewBox->hide();
    if (tocBox->createToc()) {
        qWarning() << "This document does not contain a table of contents";
        return;
    }
    if (!this->isActiveWindow())
        this->activateWindow();
    ui->notes_widget->hide();
    if (drawSlide != nullptr)
        drawSlide->hide();
    tocBox->show();
    tocBox->raise();
    tocBox->focusCurrent(currentPageNumber);
}

void ControlScreen::showNotes()
{
    tocBox->hide();
    overviewBox->hide();
    if (drawSlide == nullptr) {
        ui->notes_widget->show();
        ui->notes_widget->setFocus();
    }
    else {
        drawSlide->show();
        drawSlide->setFocus();
    }
}

void ControlScreen::showOverview()
{
    tocBox->hide();
    if (overviewBox->needsUpdate()) {
        cacheTimer->stop();
        overviewBox->create(presentation, pagePart);
    }
    if (!this->isActiveWindow())
        this->activateWindow();
    ui->notes_widget->hide();
    if (drawSlide != nullptr)
        drawSlide->hide();
    overviewBox->show();
    overviewBox->raise();
    overviewBox->setFocused(presentationScreen->getPageNumber());
}

void ControlScreen::setRenderer(QStringList const& command)
{
    // Set a command for an external renderer.
    // This function also checks whether the command uses the arguments %file, %page, %width and %height

    if (command.size() == 1 && command.first() == "poppler")
        return;
    if (
            command.filter("%file").isEmpty() ||
            command.filter("%page").isEmpty() ||
            command.filter("%width").isEmpty() ||
            command.filter("%height").isEmpty()
            ) {
        qCritical() << "Ignored request to use custom renderer. Rendering command should comtain arguments %file, %page, %width, and %height.";
        throw 2;
    }
    QString program = command.join(" ");
    presentationScreen->slide->getCacheMap()->setRenderer(program);
    ui->notes_widget->getCacheMap()->setRenderer(program);
    previewCache->setRenderer(program);
    if (drawSlideCache != nullptr)
        drawSlideCache->setRenderer(program);
    if (previewCacheX != nullptr)
        previewCacheX->setRenderer(program);
    return;
}

void ControlScreen::reloadFiles()
{
    // Stop the cache management and wait until the cache threads finish.
    interruptCacheProcesses(10000);

    /// True if files have changed.
    bool change = false;
    // Reload notes file
    if (notes->loadDocument()) {
        qInfo() << "Reloading notes file";
        change = true;
        ui->notes_widget->clearAll();
        recalcLayout(currentPageNumber);
    }
    // Reload presentation file
    if ((presentation == notes && change) || (presentation != notes && presentation->loadDocument())) {
        qInfo() << "Reloading presentation file";
        change = true;
        bool const unlimitedCache = numberOfPages==maxCacheNumber;
        numberOfPages = presentation->getDoc()->numPages();
        if (unlimitedCache)
            maxCacheNumber = numberOfPages;
        presentationScreen->updatedFile();
        ui->current_slide->clearAll();
        ui->next_slide->clearAll();
        // Hide TOC and overview and set them outdated
        showNotes();
        tocBox->setOutdated();
        tocBox->createToc();
        overviewBox->setOutdated();
    }
    // If one of the two files has changed: Reset cache region and render pages on control screen.
    if (change) {
        first_cached = currentPageNumber;
        last_cached = first_cached-1;
        first_delete = 0;
        last_delete = numberOfPages-1;
        renderPage(currentPageNumber);
        ui->text_number_slides->setText(QString::number(numberOfPages));
        ui->text_current_slide->setNumberOfPages(numberOfPages);
    }
    updateCache();
}

void ControlScreen::setKeyMap(QMap<quint32, QList<KeyAction>>* keymap)
{
    delete this->keymap;
    this->keymap = keymap;
}

void ControlScreen::setKeyMapItem(quint32 const key, KeyAction const action)
{
    // Add an action to a key
    QMap<quint32, QList<KeyAction>>::iterator map_it = keymap->find(key);
    if (map_it==keymap->end())
        keymap->insert(key, {action});
    else if (!map_it->contains(action))
        map_it->append(action);
}

void ControlScreen::showDrawSlide()
{
    // Draw slide and tool selector
    if (drawSlide == nullptr) {
        // Create the draw slide.
        //drawSlide = new DrawSlide(presentation, currentPageNumber, pagePart, this);
        drawSlide = new DrawSlide(this);
        drawSlide->setDoc(presentation, pagePart);
        // ui->notes_widget can get focus.
        drawSlide->setFocusPolicy(Qt::ClickFocus);

        // Connect drawSlide to other widgets.
        // Copy paths from draw slide to presentation slide and vice versa when drawing on one of the slides.
        // Copy paths after moving the mouse while drawing. This assumes that only the last path is changed or a new path is created.
        connect(drawSlide->getPathOverlay(), &PathOverlay::pathsChangedQuick, presentationScreen->slide->getPathOverlay(), &PathOverlay::setPathsQuick);
        connect(presentationScreen->slide->getPathOverlay(), &PathOverlay::pathsChangedQuick, drawSlide->getPathOverlay(), &PathOverlay::setPathsQuick);
        // Copy all paths. This completely updates all paths after using the erasor.
        connect(drawSlide->getPathOverlay(), &PathOverlay::pathsChanged, presentationScreen->slide->getPathOverlay(), &PathOverlay::setPaths);
        connect(presentationScreen->slide->getPathOverlay(), &PathOverlay::pathsChanged, drawSlide->getPathOverlay(), &PathOverlay::setPaths);
        // Send pointer position (when using a pointer, torch or magnifier tool).
        connect(drawSlide->getPathOverlay(), &PathOverlay::pointerPositionChanged, presentationScreen->slide->getPathOverlay(), &PathOverlay::setPointerPosition);
        connect(presentationScreen->slide->getPathOverlay(), &PathOverlay::pointerPositionChanged, drawSlide->getPathOverlay(), &PathOverlay::setPointerPosition);
        // Send relax signal when the mouse is released, which ends drawing a path.
        connect(drawSlide->getPathOverlay(), &PathOverlay::sendRelax, presentationScreen->slide->getPathOverlay(), &PathOverlay::relax);
        connect(presentationScreen->slide->getPathOverlay(), &PathOverlay::sendRelax, drawSlide->getPathOverlay(), &PathOverlay::relax);
        // Request rendering an enlarged page as required for the magnifier.
        connect(drawSlide->getPathOverlay(), &PathOverlay::sendUpdateEnlargedPage, presentationScreen->slide->getPathOverlay(), &PathOverlay::updateEnlargedPage);
        connect(presentationScreen->slide->getPathOverlay(), &PathOverlay::sendUpdateEnlargedPage, drawSlide->getPathOverlay(), &PathOverlay::updateEnlargedPage);
        // Paths are drawn on a transparent QPixmap for faster rendering.
        // Signals used to request updates for this QPixmap:
        connect(drawSlide->getPathOverlay(), &PathOverlay::sendUpdatePathCache, presentationScreen->slide->getPathOverlay(), &PathOverlay::updatePathCache);
        connect(presentationScreen->slide->getPathOverlay(), &PathOverlay::sendUpdatePathCache, drawSlide->getPathOverlay(), &PathOverlay::updatePathCache);
        // drawSlide can send page change events.
        connect(drawSlide, &PreviewSlide::sendNewPageNumber, presentationScreen, &PresentationScreen::receiveNewPage);
        connect(drawSlide, &PreviewSlide::sendNewPageNumber, this, [&](int const pageNumber){renderPage(pageNumber);});

#ifdef DISABLE_TOOL_TIP
#else
        ui->current_slide->setToolTip(preview1_tooltip);
        ui->next_slide->setToolTip(preview2_tooltip);
#endif
    }
    else if (drawSlide == ui->notes_widget)
        return;

    // drawSlide has the same muted state as the notes.
    drawSlide->setMuted(ui->notes_widget->isMuted());

    // Recalculate layout. The layout can change because presentation and notes slides can have different aspect ratios.
    // Switch common cache for preview slides if the geometry changes.
    if (presentation->getPageSize(currentPageNumber) != notes->getPageSize(currentPageNumber)) {
        // Temporarily set cache to nullptr to avoid clearing cache.
        ui->current_slide->overwriteCacheMap(nullptr);
        ui->next_slide->overwriteCacheMap(nullptr);
    }
    recalcLayout(currentPageNumber);
    // drawSlide is drawn on top of the notes widget. It should thus have the same geometry.
    if (drawSlideCache == nullptr) {
        drawSlideCache = new CacheMap(presentation, pagePart, this);
        connect(drawSlideCache, &CacheMap::cacheSizeChanged, this, &ControlScreen::updateCacheSize);
        connect(drawSlideCache, &CacheMap::cacheThreadFinished, this, &ControlScreen::cacheThreadFinished);
    }
    first_cached = currentPageNumber;
    last_cached = currentPageNumber-1;
    first_delete = 0;
    last_delete = numberOfPages-1;
#ifdef DEBUG_CACHE
    qDebug() << "Reset cache region" << first_delete << first_cached << currentPageNumber << last_cached << last_delete;
#endif
    drawSlide->overwriteCacheMap(drawSlideCache);
    // If notes slides have a different aspect ratio than presentation slides, then change preview cache to previewCacheX.
    // This cache is used because the geometry of the preview widgets will change.
    QSizeF const pressize = presentation->getPageSize(currentPageNumber), notessize = notes->getPageSize(currentPageNumber);
    if (abs(pressize.width()*notessize.height() - pressize.height()*notessize.width()) > 1e-2) {
        if (previewCacheX == nullptr) {
            previewCacheX = new CacheMap(presentation, pagePart, this);
            connect(previewCacheX, &CacheMap::cacheSizeChanged, this, &ControlScreen::updateCacheSize);
            connect(previewCacheX, &CacheMap::cacheThreadFinished, this, &ControlScreen::cacheThreadFinished);
        }
        ui->current_slide->overwriteCacheMap(previewCacheX);
        ui->next_slide->overwriteCacheMap(previewCacheX);
    }

    // Render current page on drawSlide.
    drawSlide->renderPage(presentationScreen->slide->pageNumber(), false);
    // Set the current tool on drawSlide.
    drawSlide->getPathOverlay()->setTool(presentationScreen->slide->getPathOverlay()->getTool(), presentationScreen->slide->getResolution());
    // Hide the notes and show (and focus) the drawSlide.
    ui->notes_widget->hide();
    drawSlide->show();
    drawSlide->setFocus();
    // Get offset and resolution of presentation screen.
    // This is needed to show existing drawings from the presentation screen on drawSlide.
    qint16 const sx=presentationScreen->slide->getXshift(), sy=presentationScreen->slide->getYshift();
    qreal const res = presentationScreen->slide->getResolution();
    /// Relative size of the draw slide compared to the presentation slide.
    qreal const scale = drawSlide->getResolution() / res;
    // Set eraser size on the draw slide.
    drawSlide->getPathOverlay()->setEraserSize(scale*presentationScreen->slide->getPathOverlay()->getEraserSize());
    // Get the current page label.
    QString const label = presentationScreen->slide->getPage()->label();
    // Load existing drawings from the presentation screen for the current page on drawSlide.
    drawSlide->getPathOverlay()->setPaths(label, presentationScreen->slide->getPathOverlay()->getPaths()[label], sx, sy, res);
    // Show the changed drawings.
    drawSlide->update();
    // Render the current page on drawSlide. This also adapts the current and next slide previews to previews of the next two slides.
    renderPage(currentPageNumber);
    // Set autostart delay for multimedia content on drawSlide.
    drawSlide->setAutostartDelay(presentationScreen->slide->getAutostartDelay());
    if (cacheTimer != nullptr)
        cacheTimer->start();
}

void ControlScreen::hideDrawSlide()
{
    if (drawSlide != nullptr && drawSlide != ui->notes_widget) {
        drawSlide->hide();
        drawSlide->overwriteCacheMap(nullptr);
        delete drawSlide;
        drawSlide = nullptr;
#ifdef DISABLE_TOOL_TIP
#else
        ui->current_slide->setToolTip(preview0_tooltip);
        ui->next_slide->setToolTip(preview1_tooltip);
#endif
    }
    ui->notes_widget->show();
    ui->notes_widget->setFocus();
    // Switch common cache for preview slides if the geometry changes.
    if (ui->current_slide->getCacheMap() != previewCache) {
        ui->current_slide->overwriteCacheMap(nullptr);
        ui->next_slide->overwriteCacheMap(nullptr);
    }
    recalcLayout(currentPageNumber);
    if (presentation->getPageSize(currentPageNumber) != notes->getPageSize(currentPageNumber)) {
        ui->current_slide->overwriteCacheMap(previewCache);
        ui->next_slide->overwriteCacheMap(previewCache);
    }
    renderPage(currentPageNumber);
}

void ControlScreen::setAutostartDelay(const qreal timeout)
{
    // Autostart of media on the control screen can be enabled by uncommenting the following line.
    //ui->notes_widget->setAutostartDelay(timeout);
    presentationScreen->slide->setAutostartDelay(timeout);
    if (drawSlide != nullptr)
        drawSlide->setAutostartDelay(timeout);
}

void ControlScreen::interruptCacheProcesses(const unsigned long time)
{
    cacheTimer->stop();

    // Interrupt cache threads.
    presentationScreen->slide->getCacheMap()->getCacheThread()->requestInterruption();
    ui->notes_widget->getCacheMap()->getCacheThread()->requestInterruption();
    if (previewCache != nullptr)
        previewCache->getCacheThread()->requestInterruption();
    if (previewCacheX != nullptr)
        previewCacheX->getCacheThread()->requestInterruption();
    if (drawSlideCache != nullptr)
        drawSlideCache->getCacheThread()->requestInterruption();
    SingleRenderer* singleRendererPresentation = presentationScreen->slide->getPathOverlay()->getEnlargedPageRenderer();
    if (singleRendererPresentation != nullptr)
        singleRendererPresentation->getCacheThread()->requestInterruption();
    SingleRenderer* singleRendererDrawSlide = nullptr;
    if (drawSlide != nullptr) {
        singleRendererDrawSlide = drawSlide->getPathOverlay()->getEnlargedPageRenderer();
        if (singleRendererDrawSlide != nullptr)
            singleRendererDrawSlide->getCacheThread()->requestInterruption();
    }

    if (time != 0) {
        if (previewCache != nullptr && !previewCache->getCacheThread()->wait(time))
            qWarning() << "Cache thread previewCache not stopped after" << time << "ms";
        if (previewCacheX != nullptr && !previewCacheX->getCacheThread()->wait(time))
            qWarning() << "Cache thread previewCacheX not stopped after" << time << "ms";
        if (!ui->notes_widget->getCacheMap()->getCacheThread()->wait(time))
            qWarning() << "Cache thread notes not stopped after" << time << "ms";
        if (drawSlideCache != nullptr && !drawSlideCache->getCacheThread()->wait(time))
            qWarning() << "Cache thread draw slide not stopped after" << time << "ms";
        if (!presentationScreen->slide->getCacheMap()->getCacheThread()->wait(time))
            qWarning() << "Cache thread presentation not stopped after" << time << "ms";
        if (singleRendererDrawSlide != nullptr && !singleRendererDrawSlide->getCacheThread()->wait(time))
            qWarning() << "Cache thread enlarged page draw slide not stopped after" << time << "ms";
        if (singleRendererPresentation != nullptr && !singleRendererPresentation->getCacheThread()->wait(time))
            qWarning() << "Cache thread enlarged page presentation not stopped after" << time << "ms";
        if (previewCache != nullptr)
            previewCache->getCacheThread()->exit();
        if (previewCacheX != nullptr)
            previewCacheX->getCacheThread()->exit();
        ui->notes_widget->getCacheMap()->getCacheThread()->exit();
        if (drawSlideCache != nullptr)
            drawSlideCache->getCacheThread()->exit();
        presentationScreen->slide->getCacheMap()->getCacheThread()->exit();
        if (singleRendererDrawSlide != nullptr)
            singleRendererDrawSlide->getCacheThread()->exit();
        if (singleRendererPresentation != nullptr)
            singleRendererPresentation->getCacheThread()->exit();
    }
}

void ControlScreen::setToolForKey(quint32 const key, FullDrawTool const& tool)
{
    tools[key] = tool;
    if (tool.tool == Magnifier && tool.extras.magnification < 1e-12)
        tools[key].extras.magnification = defaultToolConfig[Magnifier].extras.magnification;
    if (tool.size <= 1e-12)
        tools[key].size = defaultToolConfig[tool.tool].size;
    if (!tool.color.isValid())
        tools[key].color = defaultToolConfig[tool.tool].color;
}

void ControlScreen::distributeTools(FullDrawTool const& tool)
{
#ifdef DEBUG_TOOL_ACTIONS
    qDebug() << "set tool from tool selector" << tool.tool << tool.color << tool.size << tool.extras.magnification;
#endif
    presentationScreen->slide->getPathOverlay()->setTool(tool);
    if (drawSlide != nullptr)
        drawSlide->getPathOverlay()->setTool(tool, presentationScreen->slide->getResolution());
}

void ControlScreen::distributeStylusTools(FullDrawTool const& tool)
{
#ifdef DEBUG_TOOL_ACTIONS
    qDebug() << "set tool from tool selector" << tool.tool << tool.color << tool.size << tool.extras.magnification;
#endif
    presentationScreen->slide->getPathOverlay()->setStylusTool(tool);
    if (drawSlide != nullptr)
        drawSlide->getPathOverlay()->setStylusTool(tool, presentationScreen->slide->getResolution());
}
