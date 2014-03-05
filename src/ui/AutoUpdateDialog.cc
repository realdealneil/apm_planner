#include "QsLog.h"
#include "AutoUpdateDialog.h"
#include "ui_AutoUpdateDialog.h"
#include <QMessageBox>
#include <QDesktopServices>
#include <QPushButton>

AutoUpdateDialog::AutoUpdateDialog(const QString &version, const QString &targetFilename,
                                   const QString &url, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::AutoUpdateDialog),
    m_sourceUrl(url),
    m_targetFilename(targetFilename),
    m_networkReply(NULL),
    m_skipVersion(FALSE),
    m_skipVersionString(version)
{
    ui->setupUi(this);
    ui->progressBar->hide();
    ui->versionLabel->setText(version);

    connect(ui->skipPushButton, SIGNAL(clicked()), this, SLOT(skipClicked()));
    connect(ui->yesPushButton, SIGNAL(clicked()), this, SLOT(yesClicked()));
    connect(ui->noPushButton, SIGNAL(clicked()), this, SLOT(noClicked()));
}

AutoUpdateDialog::~AutoUpdateDialog()
{
    delete ui;
}

void AutoUpdateDialog::noClicked()
{
    deleteLater();
    reject();
}

void AutoUpdateDialog::skipClicked()
{
    emit autoUpdateCancelled(m_skipVersionString);
}

void AutoUpdateDialog::yesClicked()
{
    startDownload(m_sourceUrl, m_targetFilename);
}

bool AutoUpdateDialog::skipVersion()
{
    return m_skipVersion;
}

bool AutoUpdateDialog::startDownload(const QString& url, const QString& filename)
{
    QString targetDir = QDesktopServices::storageLocation(QDesktopServices::DesktopLocation);

    if (filename.isEmpty())
        return false;

    if (QFile::exists(targetDir + "/" + filename)) {
        int result = QMessageBox::question(this, tr("HTTP"),
                      tr("There already exists a file called %1 in "
                         "%2. Overwrite?").arg(filename, targetDir),
                      QMessageBox::Yes|QMessageBox::No, QMessageBox::No);

        if (result == QMessageBox::No){
            return false;
        }
    }
    // Always must remove file before proceeding
    QFile::remove(targetDir + filename);

    m_targetFile = new QFile(targetDir + "/" + filename);

    if (!m_targetFile->open(QIODevice::WriteOnly)) {
        QMessageBox::information(this, tr("HTTP"),
                                 tr("Unable to save the file %1: %2.")
                                 .arg(filename).arg(m_targetFile->errorString()));
        delete m_targetFile;
        m_targetFile = NULL;
        return false;
    }

    QLOG_DEBUG() << "Start Downloading new version" << url;
    m_url = QUrl(url);
    startFileDownloadRequest(m_url);
    return true;
}

void AutoUpdateDialog::startFileDownloadRequest(QUrl url)
{
    ui->progressBar->show();
    ui->noPushButton->setText(tr("Cancel"));
    ui->yesPushButton->setEnabled(false);

    ui->statusLabel->setText(tr("Downloading %1").arg(m_targetFilename));
    m_httpRequestAborted = false;
    if (m_networkReply != NULL){
        delete m_networkReply;
        m_networkReply = NULL;
    }
    m_networkReply = m_networkAccessManager.get(QNetworkRequest(url));
    connect(m_networkReply, SIGNAL(finished()), this, SLOT(httpFinished()));
    connect(m_networkReply, SIGNAL(readyRead()), this, SLOT(httpReadyRead()));
    connect(m_networkReply, SIGNAL(downloadProgress(qint64,qint64)),
            this, SLOT(updateDataReadProgress(qint64,qint64)));
}

void AutoUpdateDialog::cancelDownload()
{
     ui->statusLabel->setText(tr("Download canceled."));
     m_httpRequestAborted = true;
     m_networkReply->abort();

}

 void AutoUpdateDialog::httpFinished()
 {
     bool result = false;
     if (m_httpRequestAborted) {
         if (m_targetFile) {
             m_targetFile->close();
             m_targetFile->remove();
             delete m_targetFile;
             m_targetFile = NULL;
         }
         m_networkReply->deleteLater();
        return;
     }

     m_targetFile->flush();
     m_targetFile->close();

     QVariant redirectionTarget = m_networkReply->attribute(QNetworkRequest::RedirectionTargetAttribute);
     if (m_networkReply->error()) {
         m_targetFile->remove();
         QMessageBox::information(this, tr("HTTP"),
                                  tr("Download failed: %1.")
                                  .arg(m_networkReply->errorString()));

     } else if (!redirectionTarget.isNull()) {
         QUrl newUrl = m_url.resolved(redirectionTarget.toUrl());
         if (QMessageBox::question(this, tr("HTTP"),
                                   tr("Redirect to %1 ?").arg(newUrl.toString()),
                                   QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
             m_url = newUrl;
             m_networkReply->deleteLater();
             m_targetFile->open(QIODevice::WriteOnly);
             m_targetFile->resize(0);
             startFileDownloadRequest(m_url);
             return;
         }
     } else {
         QString filename = m_targetFile->fileName();
         ui->statusLabel->setText(tr("Downloaded to %2.").arg(filename));
         result = true;
     }

     m_networkReply->deleteLater();
     m_networkReply = NULL;
     delete m_targetFile;
     m_targetFile = NULL;

     if (!result){
        ui->statusLabel->setText(tr("Download Failed"));
     }
     ui->noPushButton->setText(tr("OK"));

     this->raise();

     QTimer::singleShot(20000,this, SLOT(deleteLater()));
 }

 void AutoUpdateDialog::httpReadyRead()
 {
     // this slot gets called every time the QNetworkReply has new data.
     // We read all of its new data and write it into the file.
     // That way we use less RAM than when reading it at the finished()
     // signal of the QNetworkReply
     if (m_targetFile){
         m_targetFile->write(m_networkReply->readAll());
     }
 }

 void AutoUpdateDialog::updateDataReadProgress(qint64 bytesRead, qint64 totalBytes)
 {
     if (m_httpRequestAborted)
         return;
     ui->progressBar->setMaximum(totalBytes);
     ui->progressBar->setValue(bytesRead);
 }

