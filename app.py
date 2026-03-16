import streamlit as st
import pandas as pd
import matplotlib.pyplot as plt

st.set_page_config(page_title="encuesta en Elx", layout="wide")

st.title("encuesta en Elx")

# 1️⃣ Cargar datos
df = pd.read_csv("encuesta.csv")

# -------------------
# 2️⃣ FILTROS DINÁMICOS
# -------------------
st.sidebar.header("Filtros")

filtros = {}
for col in df.columns:
    valores = df[col].unique()
    if len(valores) <= 20:  # mostrar filtro solo si pocos valores
        elegido = st.sidebar.multiselect(f"Filtrar {col}", valores, default=list(valores))
        filtros[col] = elegido

# aplicar filtros
df_filtrado = df.copy()
for col, seleccionados in filtros.items():
    if seleccionados:
        df_filtrado = df_filtrado[df_filtrado[col].isin(seleccionados)]

# -------------------
# 3️⃣ SELECCIÓN DE VARIABLES PARA GRÁFICO
# -------------------
st.header("Crear gráfico")

var_x = st.selectbox("Variable X", df.columns)
var_y = st.selectbox("Variable Y (opcional)", ["Ninguna"] + list(df.columns))

tipo_grafico = st.selectbox("Tipo de gráfico", ["Barras", "Circular", "Histograma", "Barras agrupadas"])

# -------------------
# 4️⃣ GENERAR GRÁFICO
# -------------------
fig, ax = plt.subplots(figsize=(8,5))

if var_y == "Ninguna":
    # gráfico de 1 variable
    if tipo_grafico == "Barras":
        df_filtrado[var_x].value_counts().plot(kind="bar", ax=ax)
    elif tipo_grafico == "Circular":
        df_filtrado[var_x].value_counts().plot(kind="pie", ax=ax, autopct="%1.1f%%")
    elif tipo_grafico == "Histograma":
        df_filtrado[var_x].plot(kind="hist", ax=ax)
else:
    # gráfico cruzando dos variables
    tabla = pd.crosstab(df_filtrado[var_x], df_filtrado[var_y])
    if tipo_grafico == "Barras":
        tabla.plot(kind="bar", ax=ax)
    elif tipo_grafico == "Barras agrupadas":
        tabla.plot(kind="bar", ax=ax)
    elif tipo_grafico == "Circular":
        st.warning("Gráfico circular no soporta 2 variables, se usará barras")
        tabla.plot(kind="bar", ax=ax)

ax.set_title(f"{var_x}" + (f" vs {var_y}" if var_y != "Ninguna" else ""))
ax.set_ylabel("Frecuencia")
st.pyplot(fig)

# -------------------
# 5️⃣ INFORMACIÓN DEL FILTRO
# -------------------
st.info(f"Datos mostrados: {len(df_filtrado)} filas (de {len(df)} total)")