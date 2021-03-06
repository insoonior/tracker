﻿#include "mainwindow.h"
#include "model.h"
#include "ui_mainwindow.h"
#include <QDir>
#include <QDebug>
#include <QWebEnginePage>
#include <QWebChannel>
#include <QWebEngineScriptCollection>
#include <QFileDialog>
#include <QTableWidgetItem>
#include <QToolButton>
#include <QClipboard>
#include <QStatusBar>
#include <QMessageBox>
#include <QKeyEvent>
#include <QSplitter>
#include <QProgressBar>
#include <QSettings>

#define TWPATHLIST_INDEX_PATH       0
#define TWPATHLIST_INDEX_DISTANCE   1
#define TWPATHLIST_INDEX_DURATION   2
#define TWPATHLIST_INDEX_TRACE      3

#define TRACK_USR_DATA_TWPATHLIST   "from_trwPathList:"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    presenter = new Presenter(this, new Model());

    QSplitter *mSplitter = new QSplitter(Qt::Horizontal);

    QSplitter *lSplitter = new QSplitter(Qt::Vertical);
    lSplitter->addWidget(ui->gbTrack);
    lSplitter->addWidget(ui->gbTempPaths);

    mSplitter->addWidget(lSplitter);
    mSplitter->addWidget(ui->gbPreview);

    mSplitter->setStretchFactor(1, 1);
    setCentralWidget(mSplitter);


    QProgressBar *loadingProgress = new QProgressBar(this);
    loadingProgress->setObjectName("mapLoading");
    loadingProgress->setFixedSize(100, 15);
    loadingProgress->setAlignment(Qt::AlignCenter);
    QLabel *locationLabel = new QLabel(this);
    locationLabel->setObjectName("location");
    locationLabel->setAlignment(Qt::AlignCenter);

    QStatusBar *sbar = statusBar();
    sbar->addWidget(loadingProgress);
    sbar->addWidget(locationLabel);


    mapView = new QWebEngineView(ui->wdMap);
    ui->wdMap->layout()->addWidget(mapView);

    QWebChannel *mapChannel = new QWebChannel(ui->wdMap);
    mapChannel->registerObject(QStringLiteral("Mainwindow"), this);
    mapView->page()->setWebChannel(mapChannel);

    connect(mapView, &QWebEngineView::loadStarted, [=](){
        grabMouse(Qt::WaitCursor);
        grabKeyboard();
    });

    connect(mapView, &QWebEngineView::loadProgress, [=](int progress){
        loadingProgress->setValue(progress);
        if(progress >= 100) {
            releaseMouse();
            releaseKeyboard();
        }
    });

    connect(mapView, &QWebEngineView::loadFinished, [=](bool ok){
        if(!ok) {
            mapView->reload();
        }
    });

    mapView->load(QUrl::fromLocalFile(qApp->applicationDirPath()+ "/map/mapPreview.html"));


    ui->trwPathList->setSelectionBehavior(QAbstractItemView::SelectRows);
    QHeaderView *hevPathList = ui->trwPathList->header();
    //hevPathList->setSectionResizeMode(TWPATHLIST_INDEX_PATH, QHeaderView::Stretch);
    hevPathList->setSectionResizeMode(TWPATHLIST_INDEX_DISTANCE, QHeaderView::ResizeToContents);
    hevPathList->setSectionResizeMode(TWPATHLIST_INDEX_DURATION, QHeaderView::ResizeToContents);
    hevPathList->setSectionResizeMode(TWPATHLIST_INDEX_TRACE, QHeaderView::ResizeToContents);
    ui->trwPathList->installEventFilter(this);
    connect(ui->trwPathList, &QTreeWidget::itemChanged, this, &MainWindow::updateTotalDistanceAndDuration);


    connect(ui->pbCopyTotal, &QPushButton::clicked, [=]() {
        QString distanceAndDuration = QString::fromLocal8Bit("总共：%1,%2").arg(ui->leTotalDistance->text()).arg(ui->leTotalDuration->text());

        QClipboard *clipBoard = QApplication::clipboard();
        clipBoard->setText(distanceAndDuration);
    });

    ui->teTempPaths->setText(presenter->getParamter("Places").toString());
}

MainWindow::~MainWindow()
{
    presenter->setParameter("Places", ui->teTempPaths->toPlainText());

    delete ui;
}

void MainWindow::updateMapMousePosition(QString lon, QString lat)
{
    //qDebug() << "position: " << lon << lat;
    QStatusBar *sbar = statusBar();
    QLabel *locationLabel = sbar->findChild<QLabel *>("location");
    if(locationLabel != nullptr) {
        locationLabel->setText(lon + ',' + lat);
    }
}

void MainWindow::updateTotalDistanceAndDuration()
{
    double totalDis = 0;
    double totalDur = 0;
    for(int i=0; i<ui->trwPathList->topLevelItemCount(); i++) {
        QTreeWidgetItem *topLevelItem = ui->trwPathList->topLevelItem(i);

        totalDis += topLevelItem->text(TWPATHLIST_INDEX_DISTANCE).remove(QString::fromLocal8Bit("公里")).toDouble();
        totalDur += topLevelItem->text(TWPATHLIST_INDEX_DURATION).remove(QString::fromLocal8Bit("小时")).toDouble();
    }
    ui->leTotalDistance->setText(QString::fromLocal8Bit("%1公里").arg(totalDis));
    ui->leTotalDuration->setText(QString::fromLocal8Bit("%1小时").arg(totalDur));
}

void MainWindow::updateMapDriveRoute(QString usrData, QString startPlace, QString endPlace, bool success, QString distance, QString duration)
{
    if(usrData.startsWith(TRACK_USR_DATA_TWPATHLIST)) {

        int index = usrData.remove(TRACK_USR_DATA_TWPATHLIST).toInt();
        if(index >= 0) {
            QTreeWidgetItem *topLevelItem = ui->trwPathList->topLevelItem(index);

            QString places = startPlace + " , " + endPlace;
            QString distDisplay = success ? QString::fromLocal8Bit("%1公里").arg(QString::asprintf("%.2lf",distance.toDouble()/1000.0)) : "-";
            QString duraDisplay = success ? QString::fromLocal8Bit("%1小时").arg(QString::asprintf("%.2lf",duration.toDouble()/3600.0)) : "-";
            QTreeWidgetItem *subLevelItem = new QTreeWidgetItem({places, distDisplay, duraDisplay});
            subLevelItem->setFlags(subLevelItem->flags() | Qt::ItemIsEditable);
            subLevelItem->setToolTip(TWPATHLIST_INDEX_PATH, places);
            topLevelItem->addChild(subLevelItem);
            ui->trwPathList->expandItem(topLevelItem);
            ui->trwPathList->scrollToItem(subLevelItem);
        }
    }
}

void MainWindow::updateMapDriveRoutesTotalResult(QString usrData, bool success, QString totalDistance, QString totalDuration)
{
    releaseMouse();
    releaseKeyboard();

    qDebug() << usrData << "result:" << success << "distance:" << totalDistance << "duration:" << totalDuration;

    if(usrData.startsWith(TRACK_USR_DATA_TWPATHLIST)) {
        if(!success) {
            QMessageBox::warning(this, tr("ERROR"), QString::fromLocal8Bit("路线规划出错"));
            return ;
        }
        int index = usrData.remove(TRACK_USR_DATA_TWPATHLIST).toInt();
        if(index >= 0) {
            QTreeWidgetItem *topLevelItem = ui->trwPathList->topLevelItem(index);
            topLevelItem->setText(TWPATHLIST_INDEX_DISTANCE, QString::fromLocal8Bit("%1公里").arg(QString::asprintf("%.2lf",totalDistance.toDouble()/1000.0)));
            topLevelItem->setText(TWPATHLIST_INDEX_DURATION, QString::fromLocal8Bit("%1小时").arg(QString::asprintf("%.2lf",totalDuration.toDouble()/3600.0)));
            ui->trwPathList->scrollToItem(topLevelItem);
        }
    }
}

void MainWindow::queryMapDriveRoute(QString usrData, QString paths, QString seq)
{
    grabMouse(Qt::WaitCursor);
    grabKeyboard();
    emit getMapDriveRoutes(usrData, paths, seq);
}

void MainWindow::addPathToList(QString paths, bool query)
{
    if(paths.isEmpty()) {
        return ;
    }

    QTreeWidgetItem *totalPathsItem = new QTreeWidgetItem(QStringList(paths));
    totalPathsItem->setFlags(totalPathsItem->flags() | Qt::ItemIsEditable);
    totalPathsItem->setToolTip(TWPATHLIST_INDEX_PATH, paths);
    ui->trwPathList->addTopLevelItem(totalPathsItem);
    ui->trwPathList->scrollToItem(totalPathsItem);
    int index = ui->trwPathList->indexOfTopLevelItem(totalPathsItem);
    QString flag = QString(TRACK_USR_DATA_TWPATHLIST) + QString::number(index);

    QToolButton *tbTrace = new QToolButton(ui->trwPathList);
    tbTrace->setAutoRaise(true);
    tbTrace->setText(QString::fromLocal8Bit("规划路线"));
    connect(tbTrace, &QToolButton::clicked, [=]() {

        qDeleteAll(totalPathsItem->takeChildren());
        totalPathsItem->setText(TWPATHLIST_INDEX_DISTANCE, "");
        totalPathsItem->setText(TWPATHLIST_INDEX_DURATION, "");

        queryMapDriveRoute(flag, paths, QStringLiteral(","));
    });
    ui->trwPathList->setItemWidget(totalPathsItem, TWPATHLIST_INDEX_TRACE, tbTrace);

    if(query) {
        tbTrace->animateClick();
    }
}


QStringList MainWindow::getPathsFromList()
{
    QStringList paths;

    int topLevelItemCount = ui->trwPathList->topLevelItemCount();
    for(int i=0; i<topLevelItemCount; i++) {
        QTreeWidgetItem *topLevelItem = ui->trwPathList->topLevelItem(i);
        paths.append(topLevelItem->text(TWPATHLIST_INDEX_PATH).trimmed());
    }

    return paths;
}

void MainWindow::removeSelectedPathFromList()
{
    QList<QTreeWidgetItem *> selectedItems = ui->trwPathList->selectedItems();
    for(QTreeWidgetItem *selectedItem : selectedItems) {
        int index = ui->trwPathList->indexOfTopLevelItem(selectedItem);
        if(index >= 0) {
            qDeleteAll(selectedItem->takeChildren());
            delete ui->trwPathList->takeTopLevelItem(index);
        }
    }

    updateTotalDistanceAndDuration();
}

void MainWindow::addPathToListFromFile()
{
    QString fileName = QFileDialog::getOpenFileName(this, tr("Open"));
    if(!fileName.isEmpty())
    {
        QFile file(fileName);
        if(file.open(QIODevice::ReadOnly)) {

            ui->trwPathList->clear();

            file.seek(0);
            QByteArray aline;
            do{
                addPathToList(QString::fromUtf8(file.readLine()).trimmed(), false);
            }while(file.pos() < file.size());

            file.close();
        }
    }
}

void MainWindow::savePathToListToFile()
{
    QString fileName = QFileDialog::getSaveFileName(this, tr("Save as"));
    if(!fileName.isEmpty())
    {
        QFile file(fileName);
        if(file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {

            file.seek(0);
            QStringList paths = getPathsFromList();
            for(QString path : paths) {
                file.write(path.toUtf8() + "\r\n");
            }

            file.close();
        }
    }
}


void MainWindow::on_pbAddPathToList_clicked()
{
    QString sites = ui->teTempPaths->toPlainText();
    if(sites.isEmpty()) {
        return ;
    }

    sites = sites.trimmed();
    sites.replace(QRegExp("(\r|\n)+"), ",");

    addPathToList(sites, false);
}

void MainWindow::on_pbAddPathToListAndQuery_clicked()
{
    QString sites = ui->teTempPaths->toPlainText();
    if(sites.isEmpty()) {
        return ;
    }

    sites = sites.trimmed();
    sites.replace(QRegExp("(\r|\n)+"), ",");

    addPathToList(sites, true);
}

void MainWindow::on_pbImportSites_clicked()
{
    addPathToListFromFile();
}

void MainWindow::on_pbExportSites_clicked()
{
    savePathToListToFile();
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if((watched == ui->trwPathList) && (event->type() == QEvent::KeyRelease)) {
        QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
        int keyValue = keyEvent->key();
        if((keyValue == Qt::Key_Backspace) || (keyValue == Qt::Key_Delete)) {
            removeSelectedPathFromList();
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}


void MainWindow::showMessage(QString message)
{
    qDebug() << "show message: " << message;
}
