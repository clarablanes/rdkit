import streamlit as st
import pandas as pd
import matplotlib.pyplot as plt

st.title("Explorador de datos de la encuesta")

# Cargar datos
df = pd.read_csv("encuesta.csv")

# -------------------
# FILTROS
# -------------------

st.sidebar.header("Filtros")

filtro_variable = st.sidebar.selectbox(
    "Variable para filtrar",
    ["Ninguno"] + list(df.columns)
)

if filtro_variable != "Ninguno":
    valores = df[filtro_variable].unique()
    valor_elegido = st.sidebar.selectbox(
        "Selecciona valor",
        valores
    )
    df = df[df[filtro_variable] == valor_elegido]

# -------------------
# SELECCIÓN DE VARIABLE PARA GRÁFICO
# -------------------

st.header("Crear gráfico")

variable = st.selectbox(
    "Variable para visualizar",
    df.columns
)

# Tipo de gráfico
tipo_grafico = st.selectbox(
    "Tipo de gráfico",
    ["Barras", "Circular", "Histograma"]
)

# -------------------
# GENERAR GRÁFICO
# -------------------

fig, ax = plt.subplots()

if tipo_grafico == "Barras":
    df[variable].value_counts().plot(kind="bar", ax=ax)

elif tipo_grafico == "Circular":
    df[variable].value_counts().plot(kind="pie", ax=ax, autopct="%1.1f%%")

elif tipo_grafico == "Histograma":
    df[variable].plot(kind="hist", ax=ax)

ax.set_title(variable)

st.pyplot(fig)