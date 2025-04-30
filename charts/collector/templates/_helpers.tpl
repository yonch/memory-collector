{{/*
Expand the name of the chart.
*/}}
{{- define "collector.name" -}}
{{- default .Chart.Name .Values.nameOverride | trunc 63 | trimSuffix "-" }}
{{- end }}

{{/*
Create a default fully qualified app name.
We truncate at 63 chars because some Kubernetes name fields are limited to this (by the DNS naming spec).
If release name contains chart name it will be used as a full name.
*/}}
{{- define "collector.fullname" -}}
{{- if .Values.fullnameOverride }}
{{- .Values.fullnameOverride | trunc 63 | trimSuffix "-" }}
{{- else }}
{{- $name := default .Chart.Name .Values.nameOverride }}
{{- if contains $name .Release.Name }}
{{- .Release.Name | trunc 63 | trimSuffix "-" }}
{{- else }}
{{- printf "%s-%s" .Release.Name $name | trunc 63 | trimSuffix "-" }}
{{- end }}
{{- end }}
{{- end }}

{{/*
Create chart name and version as used by the chart label.
*/}}
{{- define "collector.chart" -}}
{{- printf "%s-%s" .Chart.Name .Chart.Version | replace "+" "_" | trunc 63 | trimSuffix "-" }}
{{- end }}

{{/*
Common labels
*/}}
{{- define "collector.labels" -}}
helm.sh/chart: {{ include "collector.chart" . }}
{{ include "collector.selectorLabels" . }}
{{- if .Chart.AppVersion }}
app.kubernetes.io/version: {{ .Chart.AppVersion | quote }}
{{- end }}
app.kubernetes.io/managed-by: {{ .Release.Service }}
{{- end }}

{{/*
Selector labels
*/}}
{{- define "collector.selectorLabels" -}}
app.kubernetes.io/name: {{ include "collector.name" . }}
app.kubernetes.io/instance: {{ .Release.Name }}
{{- end }}

{{/*
Create the name of the service account to use
*/}}
{{- define "collector.serviceAccountName" -}}
{{- if .Values.serviceAccount.create }}
{{- default (include "collector.fullname" .) .Values.serviceAccount.name }}
{{- else }}
{{- default "default" .Values.serviceAccount.name }}
{{- end }}
{{- end }}

{{/*
Return S3 environment variables
*/}}
{{- define "collector.s3EnvVars" -}}
- name: AWS_BUCKET
  value: {{ .Values.storage.s3.bucket | quote }}
- name: AWS_REGION
  value: {{ .Values.storage.s3.region | quote }}
{{- if .Values.storage.s3.endpoint }}
- name: AWS_ENDPOINT
  value: {{ .Values.storage.s3.endpoint | quote }}
{{- end }}
{{- if .Values.storage.s3.pathStyle }}
- name: AWS_VIRTUAL_HOSTED_STYLE_REQUEST
  value: "false"
{{- end }}
{{- if eq .Values.storage.s3.auth.method "secret" }}
- name: AWS_ACCESS_KEY_ID
  valueFrom:
    secretKeyRef:
      name: {{ include "collector.fullname" . }}
      key: {{ .Values.storage.s3.auth.existingSecretKeyMapping.accessKey | default "access_key_id" }}
- name: AWS_SECRET_ACCESS_KEY
  valueFrom:
    secretKeyRef:
      name: {{ include "collector.fullname" . }}
      key: {{ .Values.storage.s3.auth.existingSecretKeyMapping.secretKey | default "secret_access_key" }}
{{- else if eq .Values.storage.s3.auth.method "existing" }}
- name: AWS_ACCESS_KEY_ID
  valueFrom:
    secretKeyRef:
      name: {{ .Values.storage.s3.auth.existingSecret }}
      key: {{ .Values.storage.s3.auth.existingSecretKeyMapping.accessKey }}
- name: AWS_SECRET_ACCESS_KEY
  valueFrom:
    secretKeyRef:
      name: {{ .Values.storage.s3.auth.existingSecret }}
      key: {{ .Values.storage.s3.auth.existingSecretKeyMapping.secretKey }}
{{- end }}
{{- end }} 