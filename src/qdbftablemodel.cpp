#include "qdbftablemodel.h"

#include "qdbfrecord.h"

#include <QtCore/QDebug>

#define DBF_PREFETCH 255

namespace QDbf {
namespace Internal {

class QDbfTableModelPrivate
{
public:
    QDbfTableModelPrivate(const QString &dbfFileName,
                          bool readOnly,
                          QDbfTableModel *parent);

    ~QDbfTableModelPrivate();

    int rowCount(const QModelIndex &index = QModelIndex()) const;
    int columnCount(const QModelIndex &index = QModelIndex()) const;

    Qt::ItemFlags flags(const QModelIndex &index) const;

    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole);
    QVariant data(const QModelIndex &index, int role) const;

    bool setHeaderData(int section, Qt::Orientation orientation,
                       const QVariant &value, int role = Qt::DisplayRole);
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const;

    bool canFetchMore(const QModelIndex &index = QModelIndex()) const;
    void fetchMore(const QModelIndex &index = QModelIndex());

    QDbfTableModel *const q;
    QDbfTable *const m_dbfTable;

    QDbfRecord m_record;
    QVector<QDbfRecord> m_records;
    QVector<QHash<int, QVariant> > m_headers;
    int m_deletedRecordsCount;
    int m_lastRecordIndex;
};

} // namespace Internal
} // namespace QDbf

using namespace QDbf;
using namespace QDbf::Internal;

QDbfTableModelPrivate::QDbfTableModelPrivate(const QString &dbfFileName,
                                             bool readOnly,
                                             QDbfTableModel *parent) :
    q(parent),
    m_dbfTable(new QDbfTable(dbfFileName)),
    m_deletedRecordsCount(0),
    m_lastRecordIndex(-1)
{
    const QDbfTable::OpenMode &openMode = readOnly
            ? QDbfTable::ReadOnly
            : QDbfTable::ReadWrite;

    m_dbfTable->open(openMode);
    m_record = m_dbfTable->record();
}

QDbfTableModelPrivate::~QDbfTableModelPrivate()
{
    m_dbfTable->close();
    delete m_dbfTable;
}

int QDbfTableModelPrivate::rowCount(const QModelIndex &index) const
{
    Q_UNUSED(index);
    return m_records.count();
}

int QDbfTableModelPrivate::columnCount(const QModelIndex &index) const
{
    Q_UNUSED(index);
    return m_record.count();
}

Qt::ItemFlags QDbfTableModelPrivate::flags(const QModelIndex &index) const
{
    if (!index.isValid()) {
        return Qt::ItemIsEnabled;
    }

    switch (m_dbfTable->openMode()) {
    case QDbfTable::ReadWrite:
        return Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsEditable;
    default:
        return Qt::ItemIsSelectable | Qt::ItemIsEnabled;
    }
}

bool QDbfTableModelPrivate::setData(const QModelIndex &index, const QVariant &value, int role)
{
    Q_UNUSED(value);

    if (!m_dbfTable->isOpen()) {
        return false;
    }

    if (index.isValid() && role == Qt::EditRole) {
        QVariant oldValue = m_records.at(index.row()).value(index.column());
        m_records[index.row()].setValue(index.column(), value);

        if (!m_dbfTable->updateRecordInTable(m_records.at(index.row()))) {
            m_records[index.row()].setValue(index.column(), oldValue);
            return false;
        }

        emit q->dataChanged(index, index);

        return true;
    }

    return false;
}

QVariant QDbfTableModelPrivate::data(const QModelIndex &index, int role) const
{
    if (!index.isValid()) {
        return QVariant();
    }

    if (index.row() >= rowCount() ||
        index.column() >= columnCount()) {
        return QVariant();
    }

    if (role & ~(Qt::DisplayRole | Qt::EditRole)) {
        return QVariant();
    }

    QVariant value = m_records.at(index.row()).value(index.column());

    if (role == Qt::EditRole &&
        value.type() == QVariant::String) {
        value = value.toString().trimmed();
    }

    return value;
}

bool QDbfTableModelPrivate::setHeaderData(int section, Qt::Orientation orientation,
                                          const QVariant &value, int role)
{
    if (orientation != Qt::Horizontal || section < 0 || columnCount() <= section) {
        return false;
    }

    if (m_headers.size() <= section) {
        m_headers.resize(qMax(section + 1, 16));
    }

    m_headers[section][role] = value;

    emit q->headerDataChanged(orientation, section, section);

    return true;
}

QVariant QDbfTableModelPrivate::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal) {
        QVariant value = m_headers.value(section).value(role);

        if (role == Qt::DisplayRole && !value.isValid()) {
            value = m_headers.value(section).value(Qt::EditRole);
        }

        if (value.isValid()) {
            return value;
        }

        if (role == Qt::DisplayRole && m_record.count() > section) {
            return m_record.fieldName(section);
        }
    }

    if (role == Qt::DisplayRole) {
        return section + 1;
    }

    return QVariant();
}

bool QDbfTableModelPrivate::canFetchMore(const QModelIndex &index) const
{
    if (!index.isValid() && m_dbfTable->isOpen() &&
        (m_records.size() + m_deletedRecordsCount < m_dbfTable->size())) {
        return true;
    }

    return false;
}

void QDbfTableModelPrivate::fetchMore(const QModelIndex &index)
{
    if (index.isValid()) {
        return;
    }

    if (!m_dbfTable->seek(m_lastRecordIndex)) {
        return;
    }

    const int fetchSize = qMin(m_dbfTable->size() - m_records.count() -
                               m_deletedRecordsCount, DBF_PREFETCH);

    q->beginInsertRows(index, m_records.size() + 1, m_records.size() + fetchSize);

    int fetchedRecordsCount = 0;
    while (m_dbfTable->next()) {
        const QDbfRecord record(m_dbfTable->record());
        if (record.isDeleted()) {
            ++m_deletedRecordsCount;
            continue;
        }
        m_records.append(record);
        m_lastRecordIndex = m_dbfTable->at();
        if (++fetchedRecordsCount >= fetchSize) {
            break;
        }
    }

    q->endInsertRows();
}

QDbfTableModel::QDbfTableModel(const QString &dbfFileName, bool readOnly, QObject *parent) :
    QAbstractTableModel(parent),
    d(new QDbfTableModelPrivate(dbfFileName, readOnly, this))
{
    if (canFetchMore()) {
        fetchMore();
    }
}

QDbfTableModel::~QDbfTableModel()
{
    delete d;
}

int QDbfTableModel::rowCount(const QModelIndex &index) const
{
    return d->rowCount(index);
}

int QDbfTableModel::columnCount(const QModelIndex &index) const
{
    return d->columnCount(index);
}

QVariant QDbfTableModel::data(const QModelIndex &index, int role) const
{
    return d->data(index, role);
}

bool QDbfTableModel::setHeaderData(int section, Qt::Orientation orientation, const QVariant &value, int role)
{
    return d->setHeaderData(section, orientation, value, role);
}

QVariant QDbfTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    return d->headerData(section, orientation, role);
}

Qt::ItemFlags QDbfTableModel::flags(const QModelIndex &index) const
{
    return d->flags(index);
}

bool QDbfTableModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    return d->setData(index, value, role);
}

bool QDbfTableModel::canFetchMore(const QModelIndex &index) const
{
    return d->canFetchMore(index);
}

void QDbfTableModel::fetchMore(const QModelIndex &index)
{
    d->fetchMore(index);
}